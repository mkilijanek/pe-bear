#include "AssetSelector.h"

using namespace pe_bear::updater;

namespace {

	const char* ASSET_NAME_PREFIX = "pe-bear";

	/** Longest extension first: ".app.zip" must win over ".zip". */
	struct ExtensionMapping {
		const char* suffix;
		PackageType type;
	};

	const ExtensionMapping EXTENSIONS[] = {
		{ ".app.zip", PackageMacAppZip },
		{ ".tar.xz", PackageLinuxTarXz },
		{ ".appimage", PackageLinuxAppImage },
		{ ".zip", PackageWindowsZip }
	};

	const size_t EXTENSIONS_COUNT = sizeof(EXTENSIONS) / sizeof(EXTENSIONS[0]);

	PackageType stripExtension(QString &lowerName)
	{
		for (size_t i = 0; i < EXTENSIONS_COUNT; i++) {
			const QString suffix = QLatin1String(EXTENSIONS[i].suffix);
			if (lowerName.endsWith(suffix)) {
				lowerName.chop(suffix.length());
				return EXTENSIONS[i].type;
			}
		}
		return PackageUnknown;
	}

	Platform platformFromToken(const QString &token)
	{
		if (token == QLatin1String("win") || token == QLatin1String("win32")
			|| token == QLatin1String("win64") || token == QLatin1String("windows"))
		{
			return PlatformWindows;
		}
		if (token == QLatin1String("linux")) {
			return PlatformLinux;
		}
		if (token == QLatin1String("macos") || token == QLatin1String("osx")
			|| token == QLatin1String("darwin") || token == QLatin1String("mac"))
		{
			return PlatformMacOS;
		}
		return PlatformUnknown;
	}

	Architecture architectureFromToken(const QString &token)
	{
		if (token == QLatin1String("x86") || token == QLatin1String("i386")
			|| token == QLatin1String("i686") || token == QLatin1String("win32"))
		{
			return ArchX86;
		}
		if (token == QLatin1String("x64") || token == QLatin1String("x86_64")
			|| token == QLatin1String("amd64") || token == QLatin1String("win64"))
		{
			return ArchX64;
		}
		if (token == QLatin1String("arm64") || token == QLatin1String("aarch64")) {
			return ArchArm64;
		}
		return ArchUnknown;
	}

	bool qtMajorFromToken(const QString &token, int &qtMajor)
	{
		if (!token.startsWith(QLatin1String("qt"))) return false;
		const QString rest = token.mid(2);
		if (rest.isEmpty()) return false;

		const QStringList parts = rest.split(QLatin1Char('.'));
		const QString majorStr = parts.at(0);
		for (int i = 0; i < majorStr.length(); i++) {
			const QChar c = majorStr.at(i);
			if (c < QLatin1Char('0') || c > QLatin1Char('9')) return false;
		}
		bool ok = false;
		const int value = majorStr.toInt(&ok);
		if (!ok || value <= 0) return false;
		qtMajor = value;
		return true;
	}

	bool runtimeFromToken(const QString &token, QString &runtime)
	{
		/* "vs10", "vs17", "vs22" */
		if (!token.startsWith(QLatin1String("vs")) || token.length() < 3) return false;
		for (int i = 2; i < token.length(); i++) {
			const QChar c = token.at(i);
			if (c < QLatin1Char('0') || c > QLatin1Char('9')) return false;
		}
		runtime = token;
		return true;
	}

}; // namespace

bool AssetSelector::parseAssetName(const QString &name, ReleaseAsset &asset)
{
	if (name.isEmpty()) return false;

	QString lower = name.toLower();
	const PackageType packageType = stripExtension(lower);
	if (packageType == PackageUnknown) return false;

	if (!lower.startsWith(QLatin1String(ASSET_NAME_PREFIX))) return false;

	const QStringList tokens = lower.split(QLatin1Char('_'));
	/* PE-bear, version, qtN, arch, os -> five at the very least */
	if (tokens.size() < 5) return false;

	Platform platform = PlatformUnknown;
	Architecture arch = ArchUnknown;
	int qtMajor = 0;
	QString runtime;
	bool duplicateField = false;

	for (int i = 1; i < tokens.size(); i++) {
		const QString token = tokens.at(i);
		if (token.isEmpty()) return false;

		/* "x86_64" arrives split in two by the underscore separator */
		if (token == QLatin1String("x86") && (i + 1) < tokens.size()
			&& tokens.at(i + 1) == QLatin1String("64"))
		{
			if (arch != ArchUnknown) duplicateField = true;
			arch = ArchX64;
			i++;
			continue;
		}

		const Platform p = platformFromToken(token);
		if (p != PlatformUnknown) {
			if (platform != PlatformUnknown) duplicateField = true;
			platform = p;
			continue;
		}
		const Architecture a = architectureFromToken(token);
		if (a != ArchUnknown) {
			if (arch != ArchUnknown) duplicateField = true;
			arch = a;
			continue;
		}
		int major = 0;
		if (qtMajorFromToken(token, major)) {
			if (qtMajor != 0) duplicateField = true;
			qtMajor = major;
			continue;
		}
		QString rt;
		if (runtimeFromToken(token, rt)) {
			if (!runtime.isEmpty()) duplicateField = true;
			runtime = rt;
			continue;
		}
		/* the version token; anything else is an unaccounted-for field and the
		   name must be treated as not understood */
		if (Version::fromString(token).isValid()) {
			continue;
		}
		return false;
	}

	if (duplicateField) return false;
	if (platform == PlatformUnknown || arch == ArchUnknown || qtMajor == 0) return false;
	if (!isPackageTypeOnPlatform(packageType, platform)) return false;

	asset.platform = platform;
	asset.arch = arch;
	asset.qtMajor = qtMajor;
	asset.packageType = packageType;
	asset.runtime = runtime;
	return true;
}

QSet<int> AssetSelector::defaultInstallablePackageTypes(const BuildProfile &profile)
{
	QSet<int> types;
	/* A build can only ever be replaced by a package of its own type, and only
	   when an installer for that type exists. When the build does not declare
	   its package type (a developer build, or a package produced before the
	   build manifest existed), or declares one nothing here can install
	   (AppImage, macOS bundle), the flow degrades to notify-only. */
	const PackageType own = profile.packageType();
	if (own == PackageWindowsZip || own == PackageLinuxTarXz) {
		types.insert(static_cast<int>(own));
	}
	return types;
}

AssetSelector::AssetSelector(const BuildProfile &profile, const QSet<int> &installablePackageTypes)
	: m_profile(profile), m_installable(installablePackageTypes)
{
}

AssetSelector::AssetSelector(const BuildProfile &profile)
	: m_profile(profile), m_installable(defaultInstallablePackageTypes(profile))
{
}

bool AssetSelector::matches(const ReleaseAsset &asset, QString &reason) const
{
	/* 1. operating system */
	if (asset.platform != m_profile.platform()) {
		reason = QLatin1String("platform ") + platformToString(asset.platform)
			+ QLatin1String(" != ") + platformToString(m_profile.platform());
		return false;
	}
	/* 2. architecture */
	if (asset.arch != m_profile.architecture()) {
		reason = QLatin1String("architecture ") + architectureToString(asset.arch)
			+ QLatin1String(" != ") + architectureToString(m_profile.architecture());
		return false;
	}
	/* 3. package format */
	if (m_profile.packageType() != PackageUnknown
		&& asset.packageType != m_profile.packageType())
	{
		reason = QLatin1String("package ") + packageTypeToString(asset.packageType)
			+ QLatin1String(" != ") + packageTypeToString(m_profile.packageType());
		return false;
	}
	/* 4. Qt major */
	if (asset.qtMajor != m_profile.qtMajor()) {
		reason = QLatin1String("Qt major ") + QString::number(asset.qtMajor)
			+ QLatin1String(" != ") + QString::number(m_profile.qtMajor());
		return false;
	}
	/* 5. runtime. An asset that declares a runtime may only replace a build
	   that declares the same one: if this build cannot state its runtime we
	   have no way to prove compatibility, so we refuse instead of guessing. */
	if (asset.runtime != m_profile.runtime()) {
		reason = QLatin1String("runtime '") + asset.runtime
			+ QLatin1String("' != '") + m_profile.runtime() + QLatin1Char('\'');
		return false;
	}
	/* 6. minimum OS version. Asset names do not carry one today; an unknown
	   value can only widen the match, never narrow it. */
	/* Whether anything here can install the kind is not a question of fit:
	   see select(), which answers it separately so the user is told the
	   truth -- "there is a new version; get it yourself" rather than "nothing
	   for you". */
	return true;
}

AssetSelector::Outcome AssetSelector::select(const ReleaseInfo &release,
	ReleaseAsset &selected, QStringList *reasons, UpdateError *digestIssue) const
{
	if (digestIssue) *digestIssue = ErrorNone;

	if (!m_profile.isComplete()) {
		if (reasons) {
			*reasons << QLatin1String("this build does not describe itself completely: ")
				+ m_profile.toString();
		}
		return NoCompatible;
	}

	QList<ReleaseAsset> candidates;
	QList<ReleaseAsset>::const_iterator itr;
	for (itr = release.assets.begin(); itr != release.assets.end(); ++itr) {
		ReleaseAsset asset = *itr;
		if (!asset.isValid()) {
			if (reasons) *reasons << asset.name + QLatin1String(": incomplete asset metadata");
			continue;
		}
		if (!parseAssetName(asset.name, asset)) {
			if (reasons) *reasons << asset.name + QLatin1String(": name does not fit the known grammar");
			continue;
		}
		QString reason;
		if (!matches(asset, reason)) {
			if (reasons) *reasons << asset.name + QLatin1String(": ") + reason;
			continue;
		}
		/* The asset is the right one but cannot be trusted: report that
		   distinctly, so the user is told the release is unverifiable rather
		   than that no package exists for their build. */
		if (!asset.hasDigest()) {
			if (digestIssue) {
				*digestIssue = asset.digestMalformed ? ErrorInvalidDigest : ErrorMissingDigest;
			}
			if (reasons) {
				*reasons << asset.name + QLatin1String(asset.digestMalformed
					? ": published SHA-256 digest is malformed"
					: ": no SHA-256 digest published");
			}
			continue;
		}
		candidates.append(asset);
	}

	if (candidates.isEmpty()) {
		return NoCompatible;
	}
	if (candidates.size() > 1) {
		if (reasons) {
			QStringList names;
			for (int i = 0; i < candidates.size(); i++) {
				names << candidates.at(i).name;
			}
			*reasons << QLatin1String("ambiguous: ") + names.join(QLatin1String(", "));
		}
		return Ambiguous;
	}
	selected = candidates.first();
	if (!m_installable.contains(static_cast<int>(selected.packageType))) {
		if (reasons) {
			*reasons << selected.name + QLatin1String(": no installer for ")
				+ packageTypeToString(selected.packageType);
		}
		return NotInstallable;
	}
	return Selected;
}
