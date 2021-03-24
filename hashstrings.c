//
// Created by paul on 11/22/19.
//

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <libconfig.h>      /* used to parse the input files */

#include "argtable3.h"      /* used to parse command line options */
#include "btree/btree.h"    /* B+ Tree support */

#include "libhashstrings.h"


/* global arg_xxx structs */
static struct {
	struct arg_lit  * help;
	struct arg_lit  * version;
	struct arg_str  * extn;
	struct arg_file * file;
    struct arg_end  * end;
} gOption;

typedef struct tSymbolEntry {
	const char *    name;
	const char *    mapsTo;
} tSymbolEntry;

tSymbolEntry        gSymbolMap[256];
unsigned int        nextFreeSymbol = 0;
const unsigned int  kSymbolOffset  = 256;

typedef struct {
    tRecord **    pointer;
    unsigned int  count;
    unsigned int  index;
} tArray;

typedef struct {
    const char *  executableName;
    const char *  prefix;
    FILE       *  outputFile;
} tGlobals;

tGlobals globals;

/*
 * lookup byte values (0-255), encoded as 7 x 9 bit fields per uint64
 * -------- ________ -------- ________ -------- ________ -------- ________
 * .ggggggg ggFFFFFF FFFeeeee eeeeDDDD DDDDDccc ccccccBB BBBBBBBa aaaaaaaa
 */

tCharMap gCharMap[ (256 / (64 / 9)) + 1 ];

const char * kHeaderPrefix =
    "/*\n"
	"    This file was automatically generated by the %s tool.\n"
    "    Please see https://github.com/paul-chambers/HashStrings\n"
    "    **** any changes you make here will be overwritten ****\n"
    "    Please edit the original file \'%s\' instead.\n"
	"*/\n"
    "\n"
    "#ifndef Once_%08x\n"
    "#define Once_%08x\n"
    "\n"
    "#include <libhashstrings.h>\n"
    "\n";

const char * kHashEnumPrefix =
        "\n"
        "typedef enum {\n"
        "    k%sUnknown = 0,\n";

const char * kHashEnumSuffix =
        "    k%sMaxIndex = %d\n"
        "} t%sIndex;\n"
        "\n";

const char * kHashMapPrefix =
        "/* pre-computed binary search tree */\n"
        "\n"
        "tRecord map%sSearch[] = {\n";

const char * kInverseMapPrefix =
    "const char * lookup%sAsString[] =\n"
    "{\n"
    "    [ k%sUnknown ] = \"(unknown)\",\n";

const char * kHeaderSuffix =
    "#endif\n"
    "\n"
    "/* end of automatically-generated file */\n";

/*****************************************/

static inline int max( int a, int b)
{
    return (a < b) ? b : a;
}

void printError( const char * format, ... )
{
    va_list args;
    char buffer[512];

    va_start(args, format);
    vsnprintf (buffer, sizeof(buffer), format, args);
    fprintf( stderr, "### %s: %s\n", globals.executableName, buffer );
    va_end(args);
}

int printParseError( const char *description, const char *filename, int lineNumber)
{
    int result = -1;

    if ( filename != NULL )
    {
        FILE * stream = fopen( filename, "r" );
        if (stream != NULL)
        {
            char line[1024];
            int  i = 1;

            do {
                fgets( line, sizeof(line), stream );
            } while ( i++ < lineNumber && !feof( stream ) );

            line[ strcspn( line, "\r\n" ) ] = '\0';
            printError( "%s in %s at line %d: \"%s\"\n",
                        description, filename, lineNumber, line);

            fclose( stream );

            result = 0;
        }
    }
    return result;
}

void printMap( void )
{
	fprintf( globals.outputFile, "uint64_t g%sCharMap[] = {\n", globals.prefix );
	for ( int i = 0; i < ((256/(64/9)) + 1); i++ )
    {
        fprintf( globals.outputFile, "    0x%016lx%c    /*", gCharMap[i], (i < (256/(64/9)))? ',' : ' ' );

        for (unsigned int shft = 0; shft < (64 - 9); shft += 9 )
        {
            unsigned int c = (gCharMap[i] >> shft) & 0x01ffL;
            if ( c < kSymbolOffset)
            {
                if ( isgraph( c ) )
                {
                    switch ( c )
                    {
                    case '\'':
                        fprintf( globals.outputFile, " \'\\'\'" );
                        break;

                    case '\\':
                        fprintf( globals.outputFile, " \'\\\'" );
                        break;

                    default:
                        fprintf( globals.outputFile, " \'%c\' ", c );
                        break;
                    }
                }
                else
                {
                    fprintf( globals.outputFile, " 0x%02X", c );
                }
            }
            else
            {
                fprintf( globals.outputFile, " (%s)", gSymbolMap[c - kSymbolOffset].name );
            }
        }
        fprintf( globals.outputFile, " */\n" );
    }
    fprintf( globals.outputFile, "};\n\n" );
}

int processMapping( config_t * config )
{
	int result = 0;
    config_setting_t * mapping;

    /* start by mapping input to output,/
     * one-to-one */
    for ( unsigned int i = 0; i < 256; i++ )
    {
        setCharMap( gCharMap, i, i );
    }

    mapping = config_lookup( config, "mappings" );
    if (mapping != NULL)
    {
        if ( config_setting_is_group( mapping ) )
        {
            config_setting_t * element;
            unsigned int i = 0;
            unsigned int j;
            int ignoreCase;

	        while ( (element = config_setting_get_elem( mapping, i )) != NULL)
            {
                const char * name = config_setting_name( element );

                switch ( config_setting_type( element ) )
                {
                case CONFIG_TYPE_BOOL:
                	if ( strcasecmp( name, "ignoreCase" ) == 0 )
	                {
		                ignoreCase = config_setting_get_bool( element );
		                if ( ignoreCase )
		                {
			                for ( j = 'A'; j <= 'Z'; j++ )
			                {
                                setCharMap( gCharMap, j, tolower( j ));
			                }
		                }
	                }
                    break;

                case CONFIG_TYPE_STRING:
                    {
                        unsigned char c;
                        j = 0;

                        gSymbolMap[ nextFreeSymbol ].name   = name;
                        gSymbolMap[ nextFreeSymbol ].mapsTo = config_setting_get_string( element );

                        while ( (c = gSymbolMap[ nextFreeSymbol ].mapsTo[ j++ ]) != '\0' )
                        {
                            unsigned char next = gSymbolMap[ nextFreeSymbol ].mapsTo[ j ];

                            setCharMap( gCharMap, c, kSymbolOffset + nextFreeSymbol );

                            /* check for a range - a dash bracketed by two characters */
                            if ( c == '-' && j > 1 && next != '\0' )
                            {
                                /* If it's a dash (and it's not at the beginning or end of mapsTo),
                                 * then mark the run of characters between start and end (inclusive) */
                                while ( c <= next )
                                {
                                    setCharMap( gCharMap, c, kSymbolOffset + nextFreeSymbol );
                                    c++;
                                }
                            }
                        }
                        nextFreeSymbol++;
                    }
                    break;

                default:
                    printError( "unsupported mapping type in file \"%s\" at line %d",
                                config_setting_source_file( mapping ),
                                config_setting_source_line( mapping ) );
                    break;
                }
                i++;
            }

			fprintf( globals.outputFile, "\ntypedef enum {\n" );
			for ( i = 0; i < nextFreeSymbol; i++ )
			{
				fprintf( globals.outputFile, "    k%s%-16s = %u,\n",
                         globals.prefix, gSymbolMap[ i ].name, kSymbolOffset + i );
			}
	        fprintf( globals.outputFile, "    k%sMax\n} t%sMapping;\n\n",
                     globals.prefix, globals.prefix );

			printMap();
        }
        else
        {
            printError( "mapping is not a group in file \"%s\" at line %d",
                        config_setting_source_file( mapping ),
                        config_setting_source_line( mapping ) );
        }
    }
    return result;
}

int compareRecords( const void * a, const void * b, void * udata )
{
    int   result;
    const tRecord *  recordA = a;
    const tRecord *  recordB = b;
    (void) udata;

    result = recordA->hash < recordB->hash? -1 : (recordA->hash > recordB->hash);

    return result;
}

bool storeRecord( const void * a, void * udata )
{
    tArray * array = udata;

    if ( array->index < array->count )
    {
        array->pointer[ array->index++ ] = (tRecord *) a;
    }
    return true;
}

void fillTable( unsigned int depth,
                tRecord *    skipTable,
                tRecord **   linear,
                unsigned int offset,
                unsigned int length )
{
static tIndex index;
    unsigned int split = (length) / 2;

    if ( depth == 0 ) index = 0;

    tRecord * dest = &skipTable[index];
    index++;

    dest->hash         = linear[offset + split]->hash;
    dest->hashedString = linear[offset + split]->hashedString;
    dest->index        = linear[offset + split]->index;
    dest->lower        = kLeaf;
    dest->higher       = kLeaf;

    unsigned int lenL = split;
    if ( lenL > 0 )
    {
        dest->lower = index;
        fillTable( depth + 1, skipTable, linear, offset, lenL );
    }

    unsigned int lenH = length - (split + 1);
    if ( lenH > 0 )
    {
        dest->higher = index;
        fillTable( depth + 1, skipTable, linear, offset + split + 1, lenH );
    }
}


int processKeywords( config_t * config )
{
    int result = 0;
    config_setting_t * keywords;

    unsigned int keywordCount;
    char ** keywordArray;
    char ** hashedArray;

    tRecord * skipTable;

    keywords = config_lookup( config, "keywords" );
    if ( keywords == NULL || !config_setting_is_array( keywords ))
    {
        printError( "\'keywords\' must be a array, in file \"%s\" at line %d",
                    config_setting_source_file( keywords ),
                    config_setting_source_line( keywords ));
    }
    else
    {
        config_setting_t * element;
        const char       * keyword;
        unsigned int i = 0;
        char         buffer[100];

        struct btree * tree;
        tRecord record;

        keywordCount = config_setting_length( keywords );
        keywordArray = calloc( keywordCount, sizeof( char * ) );
        hashedArray  = calloc( keywordCount, sizeof( char * ) );
        if ( keywordArray == NULL || hashedArray == NULL)
        {
            printError( "failed to allocate memory" );
        }
        else
        {
            const char * src;
            char       * dest;
            int          cnt;

            for ( i = 0; i < keywordCount; i++ )
            {
                element = config_setting_get_elem( keywords, i );
                if ( element != NULL)
                {
                    if ( config_setting_type( element ) != CONFIG_TYPE_STRING )
                    {
                        printError( "keyword must be a string, in file \"%s\" at line %d",
                                    config_setting_source_file( element ),
                                    config_setting_source_line( element ));
                    }
                    else
                    {
                        keyword = config_setting_get_string( element );
                        if ( keyword != NULL)
                        {
                            src   = keyword;
                            dest  = buffer;
                            cnt   = sizeof( buffer );

                            while ( cnt > 1 && *src != '\0' && *src != ',' && *src != ';' )
                            {
                                *dest++ = *src++;
                                --cnt;
                            }
                            *dest = '\0';
                            keywordArray[i] = strdup( buffer );

                            if ( *src != '\0' )
                            {
                                hashedArray[i] = strdup( ++src );
                            }
                            else
                            {
                                hashedArray[i] = keywordArray[i];
                            }
                        }
                    }
                }
            }

            /* emit the enum */
            fprintf( globals.outputFile, kHashEnumPrefix, globals.prefix );
            for ( i = 0; i < keywordCount; i++ )
            {
                fprintf( globals.outputFile,
                         "    k%s%-16s = %u,\n",
                         globals.prefix, keywordArray[i], i+1 );
            }
            fprintf( globals.outputFile, kHashEnumSuffix,
                     globals.prefix, i+1, globals.prefix );

            /* emit the enum -> string lookup */
            fprintf( globals.outputFile, kInverseMapPrefix,
                     globals.prefix, globals.prefix );
            for ( i = 0; i < keywordCount; i++ )
            {
                fprintf( globals.outputFile,
                         "    [ k%s%-16s ] = \"%s\",\n",
                         globals.prefix, keywordArray[i], keywordArray[i] );
            }
            fprintf( globals.outputFile,
                     "    [ k%sMaxIndex ] = NULL\n};\n\n",
                     globals.prefix );

            /* create a b-tree */
            tree = btree_new( sizeof( tRecord ), 0, compareRecords, &globals );

            for ( i = 0; i < keywordCount; i++ )
            {
                src = hashedArray[i];
                while ( *src != '\0' )
                {
                    tHash hash = 0;
                    const char * hashedString = src;
                    while ( *src != '\0' && *src != ',' )
                    {
                        hash = hashChar( hash, remapChar( gCharMap, *src ));
                        src++;
                    }

                    /* insert into B+Tree */
                    record.hash         = hash;
                    record.hashedString = strndup( hashedString, src - hashedString );
                    record.index        = i;
                    btree_set( tree, &record );

                    if ( *src != '\0' )
                    { ++src; }
                }
            }

            tArray array;
            array.count = btree_count( tree );
            if ( array.count > 0 )
            {
                array.pointer = calloc( array.count, sizeof( tRecord * ));
                if ( array.pointer != NULL)
                {
                    array.index = 0;
                    btree_ascend( tree, NULL, storeRecord, &array );

                    skipTable = (tRecord *)calloc( array.count, sizeof( tRecord ));
                    if ( skipTable != NULL)
                    {
                        fillTable( 0, skipTable, array.pointer, 0, array.count );

                        fprintf( globals.outputFile, kHashMapPrefix, globals.prefix );

                        for ( i = 0; i < array.count; i++ )
                        {
                            int hashedLen = strlen( skipTable[i].hashedString );
                            int len = fprintf( globals.outputFile,
                                               "    { 0x%016lx, \"%s\",%*c k%s%s,",
                                               skipTable[i].hash,
                                               skipTable[i].hashedString,
                                               max( 0, 16 - hashedLen ), ' ',
                                               globals.prefix,
                                               keywordArray[skipTable[i].index] );
                            fprintf( globals.outputFile,"%*c %2u, %2u },\n",
                                     max( 0, 78 - len ), ' ',
                                     skipTable[i].lower, skipTable[i].higher );
                        }

                        fprintf( globals.outputFile, "};\n\n" );
#if 0
                        /* do a quick sanity check */
                        for ( i = 0; i < array.count; i++ )
                        {
                            tIndex index = findHash( skipTable, array.pointer[i]->hash );

                            fprintf( stderr, "0x%016lx ", array.pointer[i]->hash );
                            if ( index < array.count )
                            {
                                tRecord * r = &skipTable[index];
                                fprintf( stderr, "k%s%s, \"%s\"\n",
                                         globals.prefix, keywordArray[r->index], r->hashedString );
                            } else {
                                fprintf( stderr, "not found\n" );
                            }
                        }
#endif
                    }
                }
            }
        } /* allocation of arrays succeeded */
    } /* keywords is a valid array */

    return result;
}

int processStructure( config_t * config )
{
	int result;

	config_lookup_string( config, "prefix", &globals.prefix );

    /* first, we need to build the character mapping */
    result = processMapping( config );

    /* array is complete, so now we can generate the hashes */
    if ( result == 0 )
    {
    	result = processKeywords( config );
    }

    return result;
}

int processHashFile( const char *filename )
{
    int result;
    struct config_t config;

    config_init( &config );

    if ( config_read_file( &config, filename ) == CONFIG_TRUE )
	{
        struct timespec time;
        clock_gettime( CLOCK_REALTIME, &time );
        long stamp = time.tv_sec ^ time.tv_nsec;

		fprintf( globals.outputFile, kHeaderPrefix,
                 globals.executableName, filename, stamp, stamp );
		result = processStructure( &config );
		fprintf( globals.outputFile, "%s", kHeaderSuffix );
	}
    else
    {
        /* result is 0 only if the error message was output successfully  */
        /* note that config_error_file returns NULL if file doesn't exist */
        result = printParseError( config_error_text( &config ),
                                  config_error_file( &config ),
                                  config_error_line( &config ) );
        if ( result != 0 )
        {
            printError( "unable to parse \'%s\': %s",
                        filename, config_error_text( &config ) );
        }
        result = -1;
    }

    config_destroy( &config );

    return result;
}

int main( int argc, char * argv[] )
{
    int result = 0;

    globals.executableName = strrchr( argv[0], '/' );
    /* If we found a slash, increment past it.
     * If there's no slash, point at the full argv[0] */
    if ( globals.executableName++ == NULL )
        { globals.executableName = argv[0]; }

    globals.outputFile = stdout;

    /* the global arg_xxx structs above are initialised within the argtable */
    void * argtable[] =
    {
        gOption.help    = arg_litn( NULL, "help",
                                    0, 1,
                                    "display this help (and exit)"),
        gOption.version = arg_litn( NULL, "version",
                                    0, 1,
                                    "display version info (and exit)"),
	    gOption.extn    = arg_strn( "x",  "extension",
                                    "<extension>",
                                    0, 1,
                                    "set the extension to use for output files"),
        gOption.file    = arg_filen( NULL, NULL,
                                     "<file>",
                                     1, 999,
                                     "input files"),

        gOption.end     = arg_end( 20 )
    };

    int nerrors = arg_parse( argc, argv, argtable );

    /* special case: '--help' takes precedence over everything else */
    if ( gOption.help->count > 0 )
    {
        fprintf( stdout, "Usage: %s", globals.executableName );
        arg_print_syntax( stdout, argtable, "\n" );
        fprintf( stdout, "process hash file into a header file.\n\n" );
        arg_print_glossary( stdout, argtable, "  %-25s %s\n" );
        fprintf( stdout, "\n" );

        result = 0;
    }
    else if ( gOption.version->count > 0 )   /* ditto for '--version' */
    {
        fprintf( stdout, "%s, version %s\n", globals.executableName, "(to do)" );
    }
    else if (nerrors > 0) 	/* If the parser returned any errors then display them and exit */
    {
        /* Display the error details contained in the arg_end struct.*/
        arg_print_errors( stdout, gOption.end, globals.executableName );
        fprintf( stdout, "Try '%s --help' for more information.\n", globals.executableName );
        result = 1;
    }
    else
    {
        result = 0;
        int i  = 0;

        globals.outputFile = NULL;

	    const char * extension = ".h";
	    if ( gOption.extn->count != 0 )
	    {
		    extension = *gOption.extn->sval;
	    }

        while ( i < gOption.file->count && result == 0 )
        {
	        char output[FILENAME_MAX];
	        strncpy( output, gOption.file->filename[i], sizeof(output) );


	        char * p = strrchr( output, '.' );
            if ( p != NULL )
	        {
		        strncpy( p, extension, &output[ sizeof(output) - 1 ] - p );
	        }

	        globals.outputFile = fopen( output, "w" );
	        if ( globals.outputFile == NULL)
	        {
		        fprintf( stderr, "### unable to open \'%s\' (%d: %s)\n",
		                 output, errno, strerror(errno) );
		        result = errno;
	        }

			if ( result == 0 )
			{
				result = processHashFile( gOption.file->filename[i] );
			}
            i++;

			fclose( globals.outputFile );
        }
    }

    /* release each non-null entry in argtable[] */
    arg_freetable( argtable, sizeof(argtable) / sizeof(argtable[0]) );

    return result;
}
