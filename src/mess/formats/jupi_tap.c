/********************************************************************

Support for Jupiter Ace .tap cassette images

For more information see:
- http://www.jupiter-ace.co.uk/faq_ace_tap_format.html
- http://www.jupiter-ace.co.uk/doc_AceTapeFormat.html

********************************************************************/
#include "jupi_tap.h"


#define SMPLO	-32768
#define SILENCE	0
#define SMPHI	32767


static int cas_size;


/*******************************************************************
   Generate one high-low cycle of sample data
********************************************************************/
INLINE int jupiter_tap_cycle(INT16 *buffer, int sample_pos, int high, int low)
{
	int i = 0;

	if ( buffer )
	{
		while( i < high)
		{
			buffer[ sample_pos + i ] = SMPHI;
			i++;
		}

		while( i < high + low )
		{
			buffer[ sample_pos + i ] = SMPLO;
			i++;
		}
	}
	return high + low;
}


INLINE int jupiter_tap_silence(INT16 *buffer, int sample_pos, int samples)
{
	int i = 0;

	if ( buffer )
	{
		while( i < samples )
		{
			buffer[ sample_pos + i ] = SILENCE;
			i++;
		}
	}
	return samples;
}


static int jupiter_handle_tap(INT16 *buffer, const UINT8 *casdata)
{
	int	data_pos, sample_count;

	/* Make sure the file starts with a valid header */
	if ( cas_size < 0x1C )
		return -1;
	if ( casdata[0] != 0x1A || casdata[1] != 0x00 )
		return -1;

	data_pos = 0;
	sample_count = 0;

	while( data_pos < cas_size )
	{
		UINT16	block_size;
		int		i;

		/* Handle a block of tape data */
		block_size = casdata[data_pos] + ( casdata[data_pos + 1] << 8 );
		data_pos += 2;
		
		/* Make sure there are enough bytes left */
		if ( data_pos > cas_size )
			return -1;

		/* 2 seconds silence */
		sample_count += jupiter_tap_silence( buffer, sample_count, 2 * 44100 );

		/* Add pilot tone samples: 4096 for header, 512 for data */
		for( i = ( block_size == 0x001A ) ? 4096 : 512; i; i-- )
			sample_count += jupiter_tap_cycle( buffer, sample_count, 27, 27 );

		/* Sync samples */
		sample_count += jupiter_tap_cycle( buffer, sample_count, 8, 11 );

		/* Data samples */
		for ( ; block_size ; data_pos++, block_size-- )
		{
			UINT8	data = casdata[data_pos];

			for ( i = 0; i < 8; i++ )
			{
				if ( data & 0x80 )
					sample_count += jupiter_tap_cycle( buffer, sample_count, 21, 22 );
				else
					sample_count += jupiter_tap_cycle( buffer, sample_count, 10, 11 );

				data <<= 1;
			}
		}

		/* End mark samples */
		sample_count += jupiter_tap_cycle( buffer, sample_count, 12, 57 );

		/* 3 seconds silence */
		sample_count += jupiter_tap_silence( buffer, sample_count, 3 * 44100 );
	}
	return sample_count;
}


/*******************************************************************
   Generate samples for the tape image
********************************************************************/
static int jupiter_tap_fill_wave(INT16 *buffer, int sample_count, UINT8 *bytes)
{
	return jupiter_handle_tap( buffer, bytes );
}


/*******************************************************************
   Calculate the number of samples needed for this tape image
********************************************************************/
static int jupiter_tap_to_wav_size(const UINT8 *casdata, int caslen)
{
	cas_size = caslen;

	return jupiter_handle_tap( NULL, casdata );
}


static const struct CassetteLegacyWaveFiller jupiter_legacy_fill_wave =
{
	jupiter_tap_fill_wave,					/* fill_wave */
	-1,										/* chunk_size */
	0,										/* chunk_samples */
	jupiter_tap_to_wav_size,				/* chunk_sample_calc */
	44100,									/* sample_frequency */
	0,										/* header_samples */
	0										/* trailer_samples */
};


static casserr_t jupiter_tap_identify(cassette_image *cassette, struct CassetteOptions *opts)
{
	return cassette_legacy_identify(cassette, opts, &jupiter_legacy_fill_wave);
}


static casserr_t jupiter_tap_load(cassette_image *cassette)
{
	return cassette_legacy_construct(cassette, &jupiter_legacy_fill_wave);
}


static const struct CassetteFormat jupiter_tap_format =
{
	"tap",
	jupiter_tap_identify,
	jupiter_tap_load,
	NULL
};


CASSETTE_FORMATLIST_START(jupiter_cassette_formats)
	CASSETTE_FORMAT(jupiter_tap_format)
CASSETTE_FORMATLIST_END
