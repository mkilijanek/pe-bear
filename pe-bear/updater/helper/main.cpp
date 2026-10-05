/*
 * pe-bear-updater -- the process that replaces a PE-bear installation.
 *
 * Separate from PE-bear because on Windows a running executable's image
 * cannot be renamed or deleted, so the program being replaced cannot be the
 * one doing the replacing. Everything it is allowed to do is decided in
 * UpdateHelper; this file only assembles the real implementations, reads the
 * instructions, and turns the outcome into an exit code and a log.
 *
 * It links no Qt Widgets, and that part is enforced: it must run with no
 * display, and it must never be able to put a dialog in front of the user,
 * least of all one asking for administrator rights. Qt Network does come along
 * with pebear_update_core, which needs it for the download side -- but nothing
 * reachable from here opens a socket. The helper installs a package that is
 * already on disk and does not know how to fetch one.
 */

#include <QtCore>
#include <iostream>

#include "../FileSystem.h"
#include "../UpdatePaths.h"
#include "../HelperHandoff.h"
#include "../UpdateHelper.h"
#include "../DirectoryInstaller.h"
#include "../ProcessControl.h"
#include "../Version.h"

#ifdef PEBEAR_WITH_LIBARCHIVE
	#include "../LibArchiveReader.h"
#endif

using namespace pe_bear::updater;

namespace {

	const char* USAGE =
		"pe-bear-updater -- installs a PE-bear update prepared by PE-bear itself.\n"
		"\n"
		"Usage: pe-bear-updater --handoff <file>\n"
		"\n"
		"  --handoff <file>  instructions written by PE-bear, inside its private\n"
		"                    update directory. Not a general-purpose installer:\n"
		"                    the package, the target and the instructions are all\n"
		"                    re-checked here and anything unexpected is refused.\n"
		"  --version         print the version and exit\n"
		"  --help            print this and exit\n";

	void say(const QString &line)
	{
		std::cout << qPrintable(line) << std::endl;
	}

	void complain(const QString &line)
	{
		std::cerr << qPrintable(line) << std::endl;
	}

	/**
	 * Writes what happened next to the package, under the run id.
	 *
	 * Per run rather than appended to one file: two updates cannot interleave
	 * in a way that makes either unreadable, and a log of a failed update is
	 * the only thing a person has to go on afterwards.
	 */
	void writeLog(IFileSystem &fs, const UpdatePaths &paths, const QString &runId,
		const QStringList &journal, UpdateHelper::Result result, const QString &error)
	{
		QStringList lines;
		lines << QLatin1String("pe-bear-updater ") + Version::current().toString()
			+ QLatin1String(" -- ") + QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
		lines << journal;
		lines << QLatin1String("result: ") + UpdateHelper::resultToString(result);
		if (!error.isEmpty()) lines << QLatin1String("error: ") + error;

		const QString name = runId.isEmpty()
			? QLatin1String("helper-unknown.log")
			: (QLatin1String("helper-") + runId + QLatin1String(".log"));
		const QString path = QDir::cleanPath(paths.root() + QDir::separator() + name);

		if (fs.writeFile(path, lines.join(QLatin1String("\n")).toUtf8() + '\n')) {
			fs.restrictToOwner(path);
		}
	}

}; // namespace

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);
	app.setApplicationName(QLatin1String("pe-bear-updater"));
	app.setApplicationVersion(Version::current().toString());

	QString handoffPath;
	const QStringList args = QCoreApplication::arguments();
	for (int i = 1; i < args.size(); i++) {
		const QString &arg = args.at(i);
		if (arg == QLatin1String("--help") || arg == QLatin1String("-h")) {
			say(QLatin1String(USAGE));
			return 0;
		}
		if (arg == QLatin1String("--version")) {
			say(Version::current().toString());
			return 0;
		}
		if (arg == QLatin1String("--handoff")) {
			if (i + 1 >= args.size()) {
				complain(QLatin1String("--handoff needs a file"));
				return UpdateHelper::resultToExitCode(UpdateHelper::RefusedInvalidRequest);
			}
			handoffPath = args.at(++i);
			continue;
		}
		/* Refused rather than ignored. An unknown argument means the caller
		   and this program disagree about what was asked for, and proceeding
		   on the part that was understood is how the wrong thing gets
		   installed. */
		complain(QLatin1String("unknown argument: ") + arg);
		return UpdateHelper::resultToExitCode(UpdateHelper::RefusedInvalidRequest);
	}

	if (handoffPath.isEmpty()) {
		complain(QLatin1String(USAGE));
		return UpdateHelper::resultToExitCode(UpdateHelper::RefusedInvalidRequest);
	}

	RealFileSystem fs;

	/* The instructions have to live in the updater's own directory. Reading
	   them from anywhere else would let this program be pointed at a file
	   somebody else chose -- and while it holds no privilege its invoker
	   lacks, it does hold a willingness to unpack an archive over a directory,
	   which is not a thing to offer to arbitrary input. */
	const QString root = fs.canonicalPath(UpdatePaths::defaultRoot());
	const QString handoffCanonical = fs.canonicalPath(handoffPath);
	if (root.isEmpty() || handoffCanonical.isEmpty()
		|| !handoffCanonical.startsWith(root + QLatin1Char('/')))
	{
		complain(QLatin1String("the instructions are not inside the updater's directory: ")
			+ QDir::toNativeSeparators(handoffPath));
		return UpdateHelper::resultToExitCode(UpdateHelper::RefusedInvalidRequest);
	}

	bool parsed = false;
	const HelperHandoff handoff = HelperHandoff::fromJson(fs.readFile(handoffCanonical), &parsed);
	if (!parsed) {
		complain(QLatin1String("the instructions could not be read"));
		return UpdateHelper::resultToExitCode(UpdateHelper::RefusedInvalidRequest);
	}

	/* Staging goes on the installation's own volume where possible, so that
	   activation is a rename rather than a copy of the whole build. */
	const UpdatePaths paths(UpdatePaths::defaultRoot(),
		UpdatePaths::preferredStagingRoot(handoff.targetDir, UpdatePaths::defaultRoot()));

#ifdef PEBEAR_WITH_LIBARCHIVE
	LibArchiveReader reader;
	IArchiveReader *readerPtr = &reader;
#else
	/* Built without a decoder. The honest outcome is a refusal that says so,
	   not a crash halfway through staging. */
	IArchiveReader *readerPtr = NULL;
#endif

	DirectoryInstaller platform(&fs, readerPtr);
	RealProcessProbe probe;
	RealProcessLauncher launcher;

	UpdateHelper helper(&fs, &platform, &probe, &launcher, paths);
	const UpdateHelper::Result result = helper.run(handoff);

	const QStringList journal = helper.journal();
	for (int i = 0; i < journal.size(); i++) {
		say(journal.at(i));
	}
	say(UpdateHelper::resultMessage(result));

	writeLog(fs, paths, handoff.runId, journal, result, helper.lastError());

	/* Removed whatever the outcome: it has been acted on, and a successful
	   instruction left lying about is one a later run could pick up. */
	fs.removeFile(handoffCanonical);

	if (result != UpdateHelper::Succeeded) {
		complain(UpdateHelper::resultToString(result) + QLatin1String(": ") + helper.lastError());
	}
	return UpdateHelper::resultToExitCode(result);
}
