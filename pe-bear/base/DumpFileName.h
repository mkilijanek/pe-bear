#pragma once

#include <QtCore>

/**
 * Names for the files PE-bear writes when dumping sections.
 *
 * Section names are 8 raw bytes from the section header, chosen by whoever built
 * the file: they may hold path separators, "..", ':' (an NTFS alternate data
 * stream) or any other character, and several sections may share one name.
 */
namespace DumpFileName {

	/** Returns the name with every character outside [A-Za-z0-9._-] replaced by '_'. */
	QString sanitize(const QString &name);

	/** "<peName>[<secIndex>_<sanitized secName>]": safe to use as a file name, and unique per section. */
	QString forSection(const QString &peName, size_t secIndex, const QString &secName);

};
