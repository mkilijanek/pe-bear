#pragma once

#include <QtCore>
#include <bearparser/bearparser.h>

/**
 * Builds minimal but structurally valid PE images in memory.
 *
 * Synthesised rather than checked in as binaries: a committed .exe in a
 * reversing tool's repository is a liability, the interesting fields stay
 * readable here, and a test can vary one field at a time to say precisely what
 * it is exercising.
 *
 * The layout is the standard one -- DOS header, PE signature, file header,
 * optional header, section headers -- with only the fields bearparser needs to
 * accept the image set. Everything else is left zeroed.
 */
namespace minimal_pe {

	/* offsets inside the image produced by build32()/build64() */
	static const int NT_HEADERS_OFFSET = 0x80;
	static const int FILE_HEADER_OFFSET = NT_HEADERS_OFFSET + 4;
	static const int OPTIONAL_HEADER_OFFSET = FILE_HEADER_OFFSET + 20;

	struct Options
	{
		Options()
			: is64bit(false), sectionCount(1), imageSize(0x2000),
			entryPoint(0x1000), addImports(false) {}

		bool is64bit;
		int sectionCount;
		quint32 imageSize;
		quint32 entryPoint;
		/** Adds a one-entry import directory, so import views have content. */
		bool addImports;
	};

	/** A PE32 or PE32+ image. Never fails; the caller checks acceptance. */
	QByteArray build(const Options &options = Options());

	inline QByteArray build32() { return build(Options()); }

	inline QByteArray build64()
	{
		Options o;
		o.is64bit = true;
		return build(o);
	}

	inline QByteArray buildWithImports()
	{
		Options o;
		o.addImports = true;
		return build(o);
	}

	/** Truncates an image mid-header, to exercise the rejection paths. */
	QByteArray buildTruncated(int keepBytes);

	/** An image whose PE signature is wrong. */
	QByteArray buildNotAPe();

}; // namespace minimal_pe
