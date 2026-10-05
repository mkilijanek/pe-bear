#include <iostream>
#include <QtGlobal>
#include <QtCore>
#include <QApplication>
#include <QTranslator>

#include "QtCompat.h"
#include "gui/windows/MainWindow.h"
#include "base/MainSettings.h"

#ifdef PEBEAR_WITH_UPDATER
	#include "updater/StartupHandshake.h"
	#include "updater/FileSystem.h"
	#include "updater/Version.h"

	/* Told to the updater's helper, not to users: an option nobody types by
	   hand, carrying the path of the file the helper is waiting on. */
	static const char* HANDSHAKE_OPTION = "--update-handshake";
#endif

/**
 * Files named on the command line.
 *
 * Every argument is a file to open -- PE-bear takes no switches, which is why
 * the updater's handshake option has to be recognised here and removed rather
 * than simply ignored: left in, it would be handed to the PE parser as a path,
 * and the first thing a freshly installed build did would be to report that it
 * could not open "--update-handshake".
 */
QStringList collectSuppliedFiles()
{
	QStringList args = QCoreApplication::arguments();
	QStringList fNames;
	for (int i = 1; i < args.length(); i++) {
#ifdef PEBEAR_WITH_UPDATER
		if (args[i] == QLatin1String(HANDSHAKE_OPTION)) {
			i++; /* and its value */
			continue;
		}
#endif
		fNames << args[i];
	}
	return fNames;
}

#ifdef PEBEAR_WITH_UPDATER
/** Path given with --update-handshake, or empty when it was not given. */
static QString handshakeRequestPath()
{
	const QStringList args = QCoreApplication::arguments();
	for (int i = 1; i < args.length() - 1; i++) {
		if (args[i] == QLatin1String(HANDSHAKE_OPTION)) return args[i + 1];
	}
	return QString();
}
#endif

int main(int argc, char *argv[])
{
	Q_INIT_RESOURCE(application);

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

	QApplication app(argc, argv);
	// workaround for a bug in Qt (not setting default font properly)
	QApplication::setFont(QApplication::font("QMessageBox"));

	// Load the settings
	MainSettings mainSettings;
	mainSettings.readPersistent();

	// Load language file
	QTranslator translator;
	QString currLanguage = mainSettings.language;
	if (currLanguage.length() == 0) {
		currLanguage = QLocale::system().name();
	}
	QString trPath = QDir::separator() + MainSettings::languageDir + QDir::separator() + currLanguage + QDir::separator() + "PELanguage.qm";
	if (translator.load(QCoreApplication::applicationDirPath() + trPath) 
		|| translator.load(mainSettings.userDataDir() + trPath))
	{
		app.installTranslator(&translator);
		mainSettings.language = currLanguage;
	} else {
		if (!mainSettings.language.startsWith("en_US")) { // en_US is built-in, so it does not require translation file
			mainSettings.language = ""; //if not US, and the language file could not be found, reset to default
		}
	}

	app.setApplicationName(TITLE);
	app.setWindowIcon(QIcon(":/main_ico.ico"));
	app.setQuitOnLastWindowClosed(true);

	MainWindow mainWin(mainSettings);
	mainWin.setIconSize(QSize(48, 48));
	mainWin.resize(950, 650);

#ifdef PEBEAR_WITH_UPDATER
	/* The updater's helper has just replaced the installation and is holding
	   the previous one, waiting to be told whether this build works. Answering
	   here and not earlier is the whole point: reaching this line means the
	   dynamic libraries resolved, the Qt platform plugin loaded, the settings
	   were readable and the main window constructed. Those are the failures a
	   bad update actually produces, and a response written before them would
	   confirm nothing but that the file is executable.
	
	   The window is never shown and the event loop never runs: this process
	   exists to answer, and showing a window would leave the user with one
	   they did not ask for. If anything above this crashed, no response is
	   written, and the helper restores the previous version -- which is
	   exactly the right outcome. */
	const QString handshakeRequest = handshakeRequestPath();
	if (!handshakeRequest.isEmpty()) {
		pe_bear::updater::RealFileSystem fs;
		pe_bear::updater::StartupHandshake handshake(&fs);
		const bool answered = handshake.respond(handshakeRequest,
			pe_bear::updater::Version::current(), true);
		return answered ? 0 : 1;
	}
#endif

	QStringList fileNames = collectSuppliedFiles();
	if (fileNames.length()) {
		mainWin.openMultiplePEs(fileNames);
	}
	mainWin.show();
	int ret = app.exec();
	return ret;
}

