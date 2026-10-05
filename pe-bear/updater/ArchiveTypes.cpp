#include "ArchiveTypes.h"

using namespace pe_bear::updater;

QString pe_bear::updater::archiveEntryKindToString(ArchiveEntry::Kind k)
{
	switch (k) {
		case ArchiveEntry::KindFile: return QLatin1String("file");
		case ArchiveEntry::KindDir: return QLatin1String("directory");
		case ArchiveEntry::KindSymlink: return QLatin1String("symlink");
		case ArchiveEntry::KindHardlink: return QLatin1String("hardlink");
		case ArchiveEntry::KindOther: return QLatin1String("unsupported");
		default: return QLatin1String("invalid");
	}
}
