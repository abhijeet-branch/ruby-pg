/*
 * pg_column_map.c - PG::ColumnMap class extension
 * $Id$
 *
 */

#include "pg.h"
#include "pg_util.h"
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

VALUE rb_mPG_BinaryEncoder;


/*
 * Document-class: PG::BinaryEncoder::Boolean < PG::SimpleEncoder
 *
 * This is the encoder class for the PostgreSQL boolean type.
 *
 * It accepts true and false. Other values will raise an exception.
 *
 */
static int
pg_bin_enc_boolean(t_pg_coder *conv, VALUE value, char *out, VALUE *intermediate, int enc_idx)
{
	char mybool;
    if (value == Qtrue) {
      mybool = 1;
    } else if (value == Qfalse) {
      mybool = 0;
    } else {
      rb_raise( rb_eTypeError, "wrong data for binary boolean converter" );
	}
	if(out) *out = mybool;
	return 1;
}

/*
 * Document-class: PG::BinaryEncoder::Int2 < PG::SimpleEncoder
 *
 * This is the encoder class for the PostgreSQL +int2+ (alias +smallint+) type.
 *
 * Non-Number values are expected to have method +to_i+ defined.
 *
 */
static int
pg_bin_enc_int2(t_pg_coder *conv, VALUE value, char *out, VALUE *intermediate, int enc_idx)
{
	if(out){
		write_nbo16(NUM2INT(*intermediate), out);
	}else{
		*intermediate = pg_obj_to_i(value);
	}
	return 2;
}

/*
 * Document-class: PG::BinaryEncoder::Int4 < PG::SimpleEncoder
 *
 * This is the encoder class for the PostgreSQL +int4+ (alias +integer+) type.
 *
 * Non-Number values are expected to have method +to_i+ defined.
 *
 */
static int
pg_bin_enc_int4(t_pg_coder *conv, VALUE value, char *out, VALUE *intermediate, int enc_idx)
{
	if(out){
		write_nbo32(NUM2LONG(*intermediate), out);
	}else{
		*intermediate = pg_obj_to_i(value);
	}
	return 4;
}

/*
 * Document-class: PG::BinaryEncoder::Int8 < PG::SimpleEncoder
 *
 * This is the encoder class for the PostgreSQL +int8+ (alias +bigint+) type.
 *
 * Non-Number values are expected to have method +to_i+ defined.
 *
 */
static int
pg_bin_enc_int8(t_pg_coder *conv, VALUE value, char *out, VALUE *intermediate, int enc_idx)
{
	if(out){
		write_nbo64(NUM2LL(*intermediate), out);
	}else{
		*intermediate = pg_obj_to_i(value);
	}
	return 8;
}

/*
 * Document-class: PG::BinaryEncoder::Timestamp < PG::SimpleEncoder
 *
 * This is a encoder class for conversion of Ruby Time objects to PostgreSQL binary timestamps.
 *
 * The following flags can be used to specify timezone interpretation:
 * * +PG::Coder::TIMESTAMP_DB_UTC+ : Send timestamp as UTC time (default)
 * * +PG::Coder::TIMESTAMP_DB_LOCAL+ : Send timestamp as local time (slower)
 *
 * Example:
 *   enco = PG::BinaryEncoder::Timestamp.new(flags: PG::Coder::TIMESTAMP_DB_UTC)
 *   enco.encode(Time.utc(2000, 1, 1))  # => "\x00\x00\x00\x00\x00\x00\x00\x00"
 *
 * String values are expected to contain a binary data with a length of 8 byte.
 *
 */
static int
pg_bin_enc_timestamp(t_pg_coder *this, VALUE value, char *out, VALUE *intermediate, int enc_idx)
{
	if(out){
		int64_t timestamp;
		struct timespec ts;

		/* second call -> write data to *out */
		if(TYPE(*intermediate) == T_STRING){
			return pg_coder_enc_to_s(this, value, out, intermediate, enc_idx);
		}

		ts = rb_time_timespec(*intermediate);
		/* PostgreSQL's timestamp is based on year 2000 and Ruby's time is based on 1970.
			* Adjust the 30 years difference. */
		timestamp = (ts.tv_sec - 10957L * 24L * 3600L) * 1000000 + (ts.tv_nsec / 1000);

		if( this->flags & PG_CODER_TIMESTAMP_DB_LOCAL ) {
			/* send as local time */
			timestamp += NUM2LL(rb_funcall(*intermediate, rb_intern("utc_offset"), 0)) * 1000000;
		}

		write_nbo64(timestamp, out);
	}else{
		/* first call -> determine the required length */
		if(TYPE(value) == T_STRING){
			return pg_coder_enc_to_s(this, value, out, intermediate, enc_idx);
		}

		if( this->flags & PG_CODER_TIMESTAMP_DB_LOCAL ) {
			/* make a local time, so that utc_offset is set */
			value = rb_funcall(value, rb_intern("getlocal"), 0);
		}
		*intermediate = value;
	}
	return 8;
}

/*
 * Document-class: PG::BinaryEncoder::FromBase64 < PG::CompositeEncoder
 *
 * This is an encoder class for conversion of base64 encoded data
 * to it's binary representation.
 *
 */
static int
pg_bin_enc_from_base64(t_pg_coder *conv, VALUE value, char *out, VALUE *intermediate, int enc_idx)
{
	int strlen;
	VALUE subint;
	t_pg_composite_coder *this = (t_pg_composite_coder *)conv;
	t_pg_coder_enc_func enc_func = pg_coder_enc_func(this->elem);

	if(out){
		/* Second encoder pass, if required */
		strlen = enc_func(this->elem, value, out, intermediate, enc_idx);
		strlen = base64_decode( out, out, strlen );

		return strlen;
	} else {
		/* First encoder pass */
		strlen = enc_func(this->elem, value, NULL, &subint, enc_idx);

		if( strlen == -1 ){
			/* Encoded string is returned in subint */
			VALUE out_str;

			strlen = RSTRING_LENINT(subint);
			out_str = rb_str_new(NULL, BASE64_DECODED_SIZE(strlen));

			strlen = base64_decode( RSTRING_PTR(out_str), RSTRING_PTR(subint), strlen);
			rb_str_set_len( out_str, strlen );
			*intermediate = out_str;

			return -1;
		} else {
			*intermediate = subint;

			return BASE64_DECODED_SIZE(strlen);
		}
	}
}

void
init_pg_binary_encoder(void)
{
	/* This module encapsulates all encoder classes with binary output format */
	rb_mPG_BinaryEncoder = rb_define_module_under( rb_mPG, "BinaryEncoder" );

	/* Make RDoc aware of the encoder classes... */
	/* dummy = rb_define_class_under( rb_mPG_BinaryEncoder, "Boolean", rb_cPG_SimpleEncoder ); */
	pg_define_coder( "Boolean", pg_bin_enc_boolean, rb_cPG_SimpleEncoder, rb_mPG_BinaryEncoder );
	/* dummy = rb_define_class_under( rb_mPG_BinaryEncoder, "Int2", rb_cPG_SimpleEncoder ); */
	pg_define_coder( "Int2", pg_bin_enc_int2, rb_cPG_SimpleEncoder, rb_mPG_BinaryEncoder );
	/* dummy = rb_define_class_under( rb_mPG_BinaryEncoder, "Int4", rb_cPG_SimpleEncoder ); */
	pg_define_coder( "Int4", pg_bin_enc_int4, rb_cPG_SimpleEncoder, rb_mPG_BinaryEncoder );
	/* dummy = rb_define_class_under( rb_mPG_BinaryEncoder, "Int8", rb_cPG_SimpleEncoder ); */
	pg_define_coder( "Int8", pg_bin_enc_int8, rb_cPG_SimpleEncoder, rb_mPG_BinaryEncoder );
	/* dummy = rb_define_class_under( rb_mPG_BinaryEncoder, "String", rb_cPG_SimpleEncoder ); */
	pg_define_coder( "String", pg_coder_enc_to_s, rb_cPG_SimpleEncoder, rb_mPG_BinaryEncoder );
	/* dummy = rb_define_class_under( rb_mPG_BinaryEncoder, "Bytea", rb_cPG_SimpleEncoder ); */
	pg_define_coder( "Bytea", pg_coder_enc_to_s, rb_cPG_SimpleEncoder, rb_mPG_BinaryEncoder );
	/* dummy = rb_define_class_under( rb_mPG_BinaryEncoder, "Timestamp", rb_cPG_SimpleEncoder ); */
	pg_define_coder( "Timestamp", pg_bin_enc_timestamp, rb_cPG_SimpleEncoder, rb_mPG_BinaryEncoder );

	/* dummy = rb_define_class_under( rb_mPG_BinaryEncoder, "FromBase64", rb_cPG_CompositeEncoder ); */
	pg_define_coder( "FromBase64", pg_bin_enc_from_base64, rb_cPG_CompositeEncoder, rb_mPG_BinaryEncoder );
}
