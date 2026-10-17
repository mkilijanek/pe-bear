/*
 * Writes the seed corpus for fuzz_pefile into the directory given as the
 * only argument: the MinimalPe fixture of the unit tests in each of its
 * shapes. Generated rather than checked in, for the reason MinimalPe.h
 * gives: no binaries in the repository, and the seeds follow the fixture.
 */
#include "MinimalPe.h"

#include <QDir>
#include <QFile>

#include <cstdio>

static bool put(const QDir &dir, const char *name, const QByteArray &image)
{
	QFile f(dir.filePath(QString::fromLatin1(name)));
	if (!f.open(QIODevice::WriteOnly) || f.write(image) != image.size()) {
		std::fprintf(stderr, "cannot write %s\n", qPrintable(f.fileName()));
		return false;
	}
	return true;
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		std::fprintf(stderr, "usage: %s <corpus directory>\n", argv[0]);
		return 2;
	}
	const QDir dir(QString::fromLocal8Bit(argv[1]));
	if (!QDir().mkpath(dir.path())) {
		std::fprintf(stderr, "cannot create %s\n", argv[1]);
		return 1;
	}

	minimal_pe::Options sections3;
	sections3.sectionCount = 3;
	minimal_pe::Options exceptions64;
	exceptions64.is64bit = true;
	exceptions64.exceptionEntries = 4;
	minimal_pe::Options imports64;
	imports64.is64bit = true;
	imports64.addImports = true;

	bool ok = true;
	ok = put(dir, "pe32.exe", minimal_pe::build32()) && ok;
	ok = put(dir, "pe64.exe", minimal_pe::build64()) && ok;
	ok = put(dir, "pe32_imports.exe", minimal_pe::buildWithImports()) && ok;
	ok = put(dir, "pe64_imports.exe", minimal_pe::build(imports64)) && ok;
	ok = put(dir, "pe32_3sections.exe", minimal_pe::build(sections3)) && ok;
	ok = put(dir, "pe64_exceptions.exe", minimal_pe::build(exceptions64)) && ok;
	ok = put(dir, "truncated_0x100.exe", minimal_pe::buildTruncated(0x100)) && ok;
	return ok ? 0 : 1;
}
