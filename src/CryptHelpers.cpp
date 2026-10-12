#include "global.h"
#include "CryptHelpers.h"
#include <ctime>
#include <cstdlib>
#if defined(VITA)
#include "arch/Vita/ArchHooks_Vita.h" // VitaBootMark
#endif

PRNGWrapper::PRNGWrapper( const struct ltc_prng_descriptor *pPRNGDescriptor )
{
	m_iPRNG = register_prng( pPRNGDescriptor );
	ASSERT( m_iPRNG >= 0 );
#if defined(VITA)
	VitaBootMark("sm:prng-registered");
#endif

	int iRet = rng_make_prng( 128, m_iPRNG, &m_PRNG, NULL );
#if defined(VITA)
	if( iRet == CRYPT_OK )
		VitaBootMark("sm:prng-sysok");
	else
		VitaBootMark("sm:prng-fallback");
#endif
#if defined(VITA)
	if( iRet != CRYPT_OK )
	{
		/* Vita has no /dev/urandom and its CLOCKS_PER_SEC is outside the
		 * range rng_get_bytes() accepts, so system seeding always fails.
		 * Seed yarrow manually from weak sources instead. This PRNG is
		 * only used for GUIDs on Vita (profile signing is disabled), so
		 * non-cryptographic seeding is acceptable. */
		iRet = prng_descriptor[m_iPRNG].start( &m_PRNG );
		VitaBootMark("sm:prng-started");
		if( iRet == CRYPT_OK )
		{
			unsigned char seed[64];
			unsigned long tv = (unsigned long) time( NULL );
			unsigned long cv = (unsigned long) clock();
			unsigned long a1 = (unsigned long)(void *) &iRet;
			unsigned long a2 = (unsigned long)(void *) seed;
			srand( (unsigned)( tv ^ cv ^ a1 ^ (a1 >> 16) ) );
			for( unsigned i = 0; i < sizeof(seed); ++i )
			{
				unsigned r = (unsigned) rand();
				seed[i] = (unsigned char)( ( r >> ((i * 7) % 24) ) ^
					( tv >> (i % 24) ) ^ ( cv >> ((i * 3) % 24) ) ^
					( a1 >> (i % 24) ) ^ ( a2 >> ((i * 5) % 24) ) );
			}
			iRet = prng_descriptor[m_iPRNG].add_entropy( seed, sizeof(seed), &m_PRNG );
			VitaBootMark("sm:prng-seeded");
			if( iRet == CRYPT_OK )
				iRet = prng_descriptor[m_iPRNG].ready( &m_PRNG );
			memset( seed, 0, sizeof(seed) );
			VitaBootMark("sm:prng-ready");
		}
	}
#endif
	ASSERT_M( iRet == CRYPT_OK, error_to_string(iRet) );
}

PRNGWrapper::~PRNGWrapper()
{
	if( m_iPRNG != -1 )
		prng_descriptor[m_iPRNG].done( &m_PRNG );
}

void PRNGWrapper::AddEntropy( const void *pData, int iSize )
{
	int iRet = prng_descriptor[m_iPRNG].add_entropy( (const unsigned char *) pData, iSize, &m_PRNG );
	ASSERT_M( iRet == CRYPT_OK, error_to_string(iRet) );

	iRet = prng_descriptor[m_iPRNG].ready( &m_PRNG );
	ASSERT_M( iRet == CRYPT_OK, error_to_string(iRet) );
}

void PRNGWrapper::AddRandomEntropy()
{
	unsigned char buf[256];
	int iRet = rng_get_bytes( buf, sizeof(buf), NULL );
	ASSERT( iRet == sizeof(buf) );

	AddEntropy( buf, sizeof(buf) );
}

RSAKeyWrapper::RSAKeyWrapper()
{
	memset( &m_Key, 0, sizeof(m_Key) );
}

RSAKeyWrapper::~RSAKeyWrapper()
{
	Unload();
}

void RSAKeyWrapper::Unload()
{
	rsa_free( &m_Key );
}

void RSAKeyWrapper::Generate( PRNGWrapper &prng, int iKeyLenBits )
{
	Unload();

	int iRet = rsa_make_key( &prng.m_PRNG, prng.m_iPRNG, iKeyLenBits / 8, 65537, &m_Key );
	ASSERT( iRet == CRYPT_OK );
}

bool RSAKeyWrapper::Load( const RString &sKey, RString &sError )
{
	Unload();

	int iRet = rsa_import( (const unsigned char *) sKey.data(), sKey.size(), &m_Key );
	if( iRet != CRYPT_OK )
	{
		memset( &m_Key, 0, sizeof(m_Key) );
		sError = error_to_string(iRet);
		return false;
	}

	return true;
}

