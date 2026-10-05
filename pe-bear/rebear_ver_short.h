#pragma once

#define REBEAR_MAJOR_VERSION 0
#define REBEAR_MINOR_VERSION 7
#define REBEAR_MICRO_VERSION 2
#define REBEAR_PATCH_VERSION 0

/* The fork's own number on top of the upstream release: 0.7.2-p001, -p002, ...
   Both come from CMake (PEBEAR_FORK_PATCH); unset, this is a plain 0.7.2. */
#ifndef REBEAR_FORK_PATCH
	#define REBEAR_FORK_PATCH 0
#endif
#ifdef REBEAR_FORK_PATCH_DESC
	#define REBEAR_VERSION_STR	"0.7.2-" REBEAR_FORK_PATCH_DESC
#else
	#define REBEAR_FORK_PATCH_DESC ""
	#define REBEAR_VERSION_STR	"0.7.2"
#endif
