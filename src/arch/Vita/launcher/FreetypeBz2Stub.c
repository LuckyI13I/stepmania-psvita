/* FreeType in the VitaSDK is built with bzip2 stream support, but the SDK
 * ships no libbz2 for arm-vita-eabi. Nothing on the console uses bzip2-wrapped
 * font files, so these three stubs satisfy the linker and make FreeType report
 * a clean error if such a face is ever opened.
 *
 * Keep them in C so the names keep their plain C linkage. */

int BZ2_bzDecompressInit( void *strm, int verbosity, int small );
int BZ2_bzDecompress( void *strm );
int BZ2_bzDecompressEnd( void *strm );

#define BZ_PARAM_ERROR ( -2 )

int BZ2_bzDecompressInit( void *strm, int verbosity, int small )
{
	(void)strm;
	(void)verbosity;
	(void)small;
	return BZ_PARAM_ERROR;
}

int BZ2_bzDecompress( void *strm )
{
	(void)strm;
	return BZ_PARAM_ERROR;
}

int BZ2_bzDecompressEnd( void *strm )
{
	(void)strm;
	return BZ_PARAM_ERROR;
}
