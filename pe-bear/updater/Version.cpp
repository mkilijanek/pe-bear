#include "Version.h"
#include "../rebear_ver_short.h"

using namespace pe_bear::updater;

const int Version::ComponentCount;
const int Version::MaxComponentDigits;

namespace {

	bool isAllDigits(const QString &s)
	{
		if (s.isEmpty()) return false;
		for (int i = 0; i < s.length(); i++) {
			const QChar c = s.at(i);
			/* deliberately ASCII-only: QChar::isDigit() also accepts other scripts */
			if (c < QLatin1Char('0') || c > QLatin1Char('9')) return false;
		}
		return true;
	}

}; // namespace

Version::Version()
	: m_forkPatch(0), m_valid(false)
{
	for (int i = 0; i < ComponentCount; i++) m_parts[i] = 0;
}

Version::Version(int major, int minor, int micro, int patch, int forkPatch)
	: m_forkPatch(forkPatch), m_valid(true)
{
	if (m_forkPatch < 0) {
		m_valid = false;
		m_forkPatch = 0;
	}
	m_parts[0] = major;
	m_parts[1] = minor;
	m_parts[2] = micro;
	m_parts[3] = patch;
	for (int i = 0; i < ComponentCount; i++) {
		if (m_parts[i] < 0) {
			m_valid = false;
			m_parts[i] = 0;
		}
	}
}

/* "-p001" and the like: the fork's suffix, which is not a prerelease. Returns
   the number, or -1 when the text carries no such suffix or a malformed one;
   `body` receives the text without it. */
static int splitForkPatch(const QString &text, QString *body)
{
	*body = text;
	const int dash = text.indexOf(QLatin1Char('-'));
	if (dash < 0) return 0;
	const QString suffix = text.mid(dash + 1);
	if (suffix.length() < 2 || !suffix.startsWith(QLatin1Char('p'))) return -1;
	const QString digits = suffix.mid(1);
	if (!isAllDigits(digits) || digits.length() > Version::MaxComponentDigits) return -1;
	*body = text.left(dash);
	return digits.toInt();
}

bool Version::looksLikePrerelease(const QString &text)
{
	QString t;
	if (splitForkPatch(text.trimmed(), &t) < 0) return true;
	/* SemVer prerelease ("-rc1") and build metadata ("+build") markers,
	   plus the loose "0.7.2rc1" and "0.7.2 beta" spellings seen in the wild */
	if (t.contains(QLatin1Char('-')) || t.contains(QLatin1Char('+'))
		|| t.contains(QLatin1Char('~')) || t.contains(QLatin1Char(' ')))
	{
		return true;
	}
	QString body = t;
	if (body.startsWith(QLatin1Char('v')) || body.startsWith(QLatin1Char('V'))) {
		body.remove(0, 1);
	}
	for (int i = 0; i < body.length(); i++) {
		const QChar c = body.at(i);
		if (c == QLatin1Char('.')) continue;
		if (c >= QLatin1Char('0') && c <= QLatin1Char('9')) continue;
		return true;
	}
	return false;
}

Version Version::fromString(const QString &text)
{
	const QString trimmed = text.trimmed();
	if (trimmed.isEmpty()) return Version();
	if (looksLikePrerelease(trimmed)) return Version();

	QString body;
	const int forkPatch = splitForkPatch(trimmed, &body);
	if (forkPatch < 0 || body.isEmpty()) return Version();
	if (body.startsWith(QLatin1Char('v')) || body.startsWith(QLatin1Char('V'))) {
		body.remove(0, 1);
	}
	if (body.isEmpty()) return Version();

	/* KeepEmptyParts: an empty component ("0..1", "0.7.") must be rejected,
	   not silently collapsed */
	const QStringList parts = body.split(QLatin1Char('.'));
	if (parts.size() < 2 || parts.size() > ComponentCount) return Version();

	int values[ComponentCount] = { 0, 0, 0, 0 };
	for (int i = 0; i < parts.size(); i++) {
		const QString &p = parts.at(i);
		if (!isAllDigits(p)) return Version();
		if (p.length() > MaxComponentDigits) return Version();
		bool ok = false;
		const int v = p.toInt(&ok);
		if (!ok || v < 0) return Version();
		values[i] = v;
	}
	return Version(values[0], values[1], values[2], values[3], forkPatch);
}

Version Version::current()
{
	return Version(REBEAR_MAJOR_VERSION, REBEAR_MINOR_VERSION,
		REBEAR_MICRO_VERSION, REBEAR_PATCH_VERSION, REBEAR_FORK_PATCH);
}

QString Version::toString() const
{
	if (!m_valid) return QString();

	int last = 2; /* always render at least major.minor.micro */
	for (int i = ComponentCount - 1; i > last; i--) {
		if (m_parts[i] != 0) {
			last = i;
			break;
		}
	}
	QString out;
	for (int i = 0; i <= last; i++) {
		if (i) out += QLatin1Char('.');
		out += QString::number(m_parts[i]);
	}
	if (m_forkPatch > 0) {
		/* Three digits, as the fork numbers them: p001, p002, ... */
		out += QLatin1String("-p") + QString::number(m_forkPatch).rightJustified(3, QLatin1Char('0'));
	}
	return out;
}

int Version::compare(const Version &other) const
{
	for (int i = 0; i < ComponentCount; i++) {
		if (m_parts[i] < other.m_parts[i]) return -1;
		if (m_parts[i] > other.m_parts[i]) return 1;
	}
	if (m_forkPatch < other.m_forkPatch) return -1;
	if (m_forkPatch > other.m_forkPatch) return 1;
	return 0;
}
