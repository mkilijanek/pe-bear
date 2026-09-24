#include "MinimalPe.h"

namespace {

	void put16(char *p, int offset, quint16 value)
	{
		qToLittleEndian<quint16>(value, reinterpret_cast<uchar*>(p + offset));
	}

	void put32(char *p, int offset, quint32 value)
	{
		qToLittleEndian<quint32>(value, reinterpret_cast<uchar*>(p + offset));
	}

	void put64(char *p, int offset, quint64 value)
	{
		qToLittleEndian<quint64>(value, reinterpret_cast<uchar*>(p + offset));
	}

}; // namespace

QByteArray minimal_pe::build(const Options &options)
{
	/* 0x600 leaves room for the headers plus one raw section */
	const int imageBytes = 0x600;
	QByteArray image(imageBytes, '\0');
	char *p = image.data();

	/* --- IMAGE_DOS_HEADER --- */
	put16(p, 0x00, 0x5A4D);                      /* e_magic: "MZ" */
	put32(p, 0x3C, NT_HEADERS_OFFSET);           /* e_lfanew */

	/* --- PE signature --- */
	put32(p, NT_HEADERS_OFFSET, 0x00004550);     /* "PE\0\0" */

	/* --- IMAGE_FILE_HEADER --- */
	const int fh = FILE_HEADER_OFFSET;
	const quint16 optionalHeaderSize = options.is64bit ? 0xF0 : 0xE0;
	put16(p, fh + 0x00, options.is64bit ? 0x8664 : 0x014C);  /* Machine */
	put16(p, fh + 0x02, quint16(options.sectionCount));      /* NumberOfSections */
	put16(p, fh + 0x10, optionalHeaderSize);                 /* SizeOfOptionalHeader */
	/* EXECUTABLE_IMAGE, plus 32BIT_MACHINE only where it is true */
	put16(p, fh + 0x12, options.is64bit ? 0x0002 : 0x0102);  /* Characteristics */

	/* --- IMAGE_OPTIONAL_HEADER32 / IMAGE_OPTIONAL_HEADER64 ---
	   ImageBase is 4 bytes on PE32 and 8 on PE32+, and the four stack/heap
	   size fields likewise, so everything past them shifts. The offsets are
	   spelled out per variant rather than tracked with a running cursor: this
	   is a fixture, and a silently misplaced field here shows up as a puzzling
	   failure in a completely different test. */
	const int oh = OPTIONAL_HEADER_OFFSET;

	/* identical in both variants */
	put16(p, oh + 0x00, options.is64bit ? 0x020B : 0x010B);  /* Magic */
	put32(p, oh + 0x10, options.entryPoint);                 /* AddressOfEntryPoint */
	put32(p, oh + 0x14, 0x1000);                             /* BaseOfCode */

	if (options.is64bit) {
		put64(p, oh + 0x18, Q_UINT64_C(0x140000000));        /* ImageBase (8) */
	} else {
		put32(p, oh + 0x1C, 0x400000);                       /* ImageBase (4) */
	}

	/* from SectionAlignment to Subsystem the layout is the same again */
	put32(p, oh + 0x20, 0x1000);                             /* SectionAlignment */
	put32(p, oh + 0x24, 0x200);                              /* FileAlignment */
	put16(p, oh + 0x28, 4);                                  /* MajorOSVersion */
	put16(p, oh + 0x2C, 1);                                  /* MajorImageVersion */
	put16(p, oh + 0x30, 4);                                  /* MajorSubsystemVersion */
	put32(p, oh + 0x38, options.imageSize);                  /* SizeOfImage */
	put32(p, oh + 0x3C, 0x200);                              /* SizeOfHeaders */
	put16(p, oh + 0x44, 2);                                  /* Subsystem: GUI */

	/* and diverges again at the stack/heap sizes */
	const int numberOfRvaOffset = options.is64bit ? (oh + 0x6C) : (oh + 0x5C);
	const int dataDirOffset = numberOfRvaOffset + 4;
	put32(p, numberOfRvaOffset, 16);                         /* NumberOfRvaAndSizes */

	/* --- section headers --- */
	const int sectionHeadersOffset = oh + optionalHeaderSize;
	for (int i = 0; i < options.sectionCount; i++) {
		const int sh = sectionHeadersOffset + (i * 40);
		if (sh + 40 > imageBytes) break;

		const QByteArray name = (i == 0)
			? QByteArray(".text")
			: QByteArray(".sec") + QByteArray::number(i);
		memcpy(p + sh, name.constData(), size_t(qMin(8, name.size())));

		put32(p, sh + 0x08, 0x1000);                         /* VirtualSize */
		put32(p, sh + 0x0C, quint32(0x1000 * (i + 1)));      /* VirtualAddress */
		put32(p, sh + 0x10, 0x200);                          /* SizeOfRawData */
		put32(p, sh + 0x14, 0x200);                          /* PointerToRawData */
		put32(p, sh + 0x24, 0x60000020);                     /* CODE|EXECUTE|READ */
	}

	if (options.addImports) {
		/* A single import descriptor for "TEST.dll", importing one name.
		   Laid out inside the first section so the RVAs resolve. */
		const quint32 importsRva = 0x1000;
		const int importsRaw = 0x200;

		put32(p, dataDirOffset + 0x08, importsRva);          /* IMPORT dir RVA */
		put32(p, dataDirOffset + 0x0C, 40);                  /* IMPORT dir size */

		/* IMAGE_IMPORT_DESCRIPTOR */
		put32(p, importsRaw + 0x00, importsRva + 0x40);      /* OriginalFirstThunk */
		put32(p, importsRaw + 0x0C, importsRva + 0x20);      /* Name */
		put32(p, importsRaw + 0x10, importsRva + 0x50);      /* FirstThunk */
		/* the terminating all-zero descriptor is already zeroed */

		memcpy(p + importsRaw + 0x20, "TEST.dll", 8);

		/* thunks -> IMAGE_IMPORT_BY_NAME at +0x60 */
		put32(p, importsRaw + 0x40, importsRva + 0x60);
		put32(p, importsRaw + 0x50, importsRva + 0x60);
		put16(p, importsRaw + 0x60, 0);                      /* Hint */
		memcpy(p + importsRaw + 0x62, "TestFunc", 8);
	}
	return image;
}

QByteArray minimal_pe::buildTruncated(int keepBytes)
{
	QByteArray full = build();
	if (keepBytes >= full.size()) return full;
	if (keepBytes < 0) keepBytes = 0;
	full.truncate(keepBytes);
	return full;
}

QByteArray minimal_pe::buildNotAPe()
{
	QByteArray image = build();
	/* keep the MZ stub, break the PE signature */
	image[NT_HEADERS_OFFSET] = 'X';
	return image;
}
