/*
 * transition_movit_luma.cpp
 * Copyright (C) 2014-2023 Dan Dennedy <dan@dennedy.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <framework/mlt.h>
#include <framework/mlt_luma_map.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#include "filter_glsl_manager.h"
#include <init.h>
#include <effect_chain.h>
#include <util.h>
#include <luma_mix_effect.h>
#include <mix_effect.h>
#include "mlt_movit_input.h"

using namespace movit;

static int get_image( mlt_frame a_frame, uint8_t **image, mlt_image_format *format, int *width, int *height, int writable )
{
	int error;

	// Get the transition object
	mlt_transition transition = (mlt_transition) mlt_frame_pop_service( a_frame );
	mlt_service service = MLT_TRANSITION_SERVICE( transition );
	// Get the b frame from the stack
	mlt_frame b_frame = (mlt_frame) mlt_frame_pop_frame( a_frame );
	mlt_frame c_frame = (mlt_frame) mlt_frame_pop_frame( a_frame );

	// Get the properties of the transition
	mlt_properties properties = MLT_TRANSITION_PROPERTIES( transition );

	mlt_service_lock( service );

	// Get the transition parameters
	mlt_position position = mlt_transition_get_position( transition, a_frame );
	mlt_position length = mlt_transition_get_length( transition );
	int reverse = mlt_properties_get_int( properties, "reverse" );
	double mix = mlt_transition_get_progress( transition, a_frame );
	double inverse = 1.0 - mix;
	double softness = mlt_properties_anim_get_double( properties, "softness", position, length );

	if ( c_frame )
	{
		// Set the Movit parameters.
		mlt_properties_set( properties, "_movit.parms.float.strength_first", nullptr );
		mlt_properties_set( properties, "_movit.parms.float.strength_second", nullptr );
		mlt_properties_set_double( properties, "_movit.parms.float.progress", reverse ? inverse : mix );
		mlt_properties_set_double( properties, "_movit.parms.float.transition_width", 1.0 / (softness + 1.0e-4) );
		mlt_properties_set_int( properties, "_movit.parms.int.inverse",
			!mlt_properties_get_int( properties, "invert" ) );

		uint8_t *a_image, *b_image, *c_image;

		// Get the images.
		*format = mlt_image_movit;
		error = mlt_frame_get_image( a_frame, &a_image, format, width, height, writable );
		error = mlt_frame_get_image( b_frame, &b_image, format, width, height, writable );
		error = mlt_frame_get_image( c_frame, &c_image, format, width, height, writable );

		if (*width < 1 || *height < 1) {
			mlt_log_error( service, "Invalid size for get_image: %dx%d", *width, *height);
			return error;
		}

		GlslManager::set_effect_input( service, a_frame, (mlt_service) a_image );
		GlslManager::set_effect_secondary_input( service, a_frame, (mlt_service) b_image, b_frame );
		GlslManager::set_effect_third_input( service, a_frame, (mlt_service) c_image, c_frame );
		GlslManager::set_effect( service, a_frame, new LumaMixEffect() );
	}
	else
	{
		// Set the Movit parameters.
		mlt_properties_set( properties, "_movit.parms.int.inverse", nullptr );
		mlt_properties_set( properties, "_movit.parms.float.progress", nullptr );
		mlt_properties_set( properties, "_movit.parms.float.transition_width", nullptr );
		mlt_properties_set_double( properties, "_movit.parms.float.strength_first", reverse ? mix : inverse );
		mlt_properties_set_double( properties, "_movit.parms.float.strength_second", reverse ? inverse : mix );
	
		uint8_t *a_image, *b_image;
	
		// Get the two images.
		*format = mlt_image_movit;
		error = mlt_frame_get_image( a_frame, &a_image, format, width, height, writable );
		error = mlt_frame_get_image( b_frame, &b_image, format, width, height, writable );

		if (*width < 1 || *height < 1) {
			mlt_log_error( service, "Invalid size for get_image: %dx%d", *width, *height);
			return error;
		}
	
		GlslManager::set_effect_input( service, a_frame, (mlt_service) a_image );
		GlslManager::set_effect_secondary_input( service, a_frame, (mlt_service) b_image, b_frame );
		GlslManager::set_effect( service, a_frame, new MixEffect() );
	}
	*image = (uint8_t *) service;
	
	mlt_service_unlock( service );
	return error;
}

static mlt_frame process( mlt_transition transition, mlt_frame a_frame, mlt_frame b_frame )
{
	mlt_properties properties = MLT_TRANSITION_PROPERTIES( transition );

	// Obtain the wipe producer.
	char *resource = mlt_properties_get( properties, "resource" );
	char *last_resource = mlt_properties_get( properties, "_resource" );
	auto producer = (mlt_producer) mlt_properties_get_data( properties, "instance", nullptr);

	// If we haven't created the wipe producer or it has changed
	if ( resource )
	if ( !producer || strcmp( resource, last_resource ) ) {
		char temp[PATH_MAX];
		const char *extension = strrchr(resource, '.');
		mlt_profile profile = mlt_service_profile(MLT_TRANSITION_SERVICE(transition));

		// Store the last resource now
		mlt_properties_set( properties, "_resource", resource );
		producer = mlt_factory_producer(profile, nullptr, resource);
		if (producer) {
			mlt_properties_set( MLT_PRODUCER_PROPERTIES( producer ), "eof", "loop" );
		}
		mlt_properties_set_data(properties, "instance", producer, 0, (mlt_destructor) mlt_producer_close, nullptr);
	}
	if ( producer ) {
		mlt_frame wipe = nullptr;
		mlt_position position = mlt_transition_get_position( transition, a_frame );
		mlt_properties_pass( MLT_PRODUCER_PROPERTIES( producer ), properties, "producer." );
		mlt_producer_seek( producer, position );
		if ( mlt_service_get_frame( MLT_PRODUCER_SERVICE( producer ), &wipe, 0 ) == 0 ) {
			char name[64];
			snprintf( name, sizeof(name), "movit.luma %s", mlt_properties_get( properties, "_unique_id" ) );
			mlt_properties_set_data( MLT_FRAME_PROPERTIES(a_frame), name, wipe, 0, (mlt_destructor) mlt_frame_close, nullptr );
			mlt_properties_set_int( MLT_FRAME_PROPERTIES(wipe), "distort", 1 );
			mlt_frame_push_frame( a_frame, wipe );
		} else {
			mlt_frame_push_frame( a_frame, nullptr );
		}
	} else {
		// We may still not have a producer in which case, dissolve
		mlt_frame_push_frame( a_frame, nullptr );
	}
	mlt_frame_push_frame( a_frame, b_frame );
	mlt_frame_push_service( a_frame, transition );
	mlt_frame_push_get_image( a_frame, get_image );

	return a_frame;
}

extern "C"
mlt_transition transition_movit_luma_init( mlt_profile profile, mlt_service_type type, const char *id, char *arg )
{
	mlt_transition transition = nullptr;
	GlslManager* glsl = GlslManager::get_instance();
	if ( glsl && ( transition = mlt_transition_new() ) ) {
		transition->process = process;
		mlt_properties_set( MLT_TRANSITION_PROPERTIES( transition ), "resource", arg );
		
		// Inform apps and framework that this is a video only transition
		mlt_properties_set_int( MLT_TRANSITION_PROPERTIES( transition ), "_transition_type", 1 );
	}
	return transition;
}
