#pragma once

#include <QtCore>
#include "UpdateTypes.h"

namespace pe_bear {
namespace updater {

/**
 * Picks the one asset of a release that is a drop-in replacement for the
 * running build.
 *
 * The selector never guesses. It parses each asset name against a fixed
 * grammar, drops everything it cannot fully account for, and requires an exact
 * match on every field it knows about. If more than one asset survives it
 * reports Ambiguous rather than taking the first one, because two matching
 * packages means the naming contract is broken and picking either could
 * migrate the user between incompatible variants.
 *
 * Asset name grammar (as published by the project today):
 *   PE-bear_<version>_qt<major>[.<minor>...]_<arch>_<os>[_<runtime>]<ext>
 *   ext: ".zip" | ".tar.xz" | ".AppImage" | ".app.zip"
 */
class AssetSelector
{
public:
	enum Outcome {
		Selected = 0,
		NoCompatible,
		Ambiguous,
		/** The right asset exists (and is returned) but nothing in this
		 *  build can install its kind: the user is told, not offered. */
		NotInstallable
	};

	/**
	 * Parses platform, architecture, package type, Qt major and runtime out of
	 * an asset file name. Returns false when the name does not fit the grammar
	 * or any mandatory field stays unknown; partially parsed output must not be
	 * used for matching.
	 */
	static bool parseAssetName(const QString &name, ReleaseAsset &asset);

	/**
	 * The package kinds this build can install: its own, and only when an
	 * installer for it exists. Today that is the whole-directory installer,
	 * which handles Windows zips and Linux tar.xz directories. An AppImage is
	 * a single file and a macOS bundle has its own rules (quarantine,
	 * /Applications), and neither has an installer yet -- those builds are
	 * told about a release and pointed at it, never offered an install.
	 */
	static QSet<int> defaultInstallablePackageTypes(const BuildProfile &profile);

	AssetSelector(const BuildProfile &profile, const QSet<int> &installablePackageTypes);
	explicit AssetSelector(const BuildProfile &profile);

	/**
	 * @param release   release whose assets to consider
	 * @param selected  receives the single matching asset on Selected
	 * @param reasons   optional, receives one human-readable line per rejected
	 *                  asset; contains only release metadata, never anything
	 *                  about files the user is analysing
	 * @param digestIssue optional, receives MissingDigest or InvalidDigest when
	 *                  an otherwise matching asset had to be dropped because it
	 *                  cannot be verified
	 */
	Outcome select(const ReleaseInfo &release, ReleaseAsset &selected,
		QStringList *reasons = NULL, UpdateError *digestIssue = NULL) const;

private:
	bool matches(const ReleaseAsset &asset, QString &reason) const;

	BuildProfile m_profile;
	QSet<int> m_installable;
};

}; // namespace updater
}; // namespace pe_bear
