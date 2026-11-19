#include "Search.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <numeric>
#include <vector>
static void initializeSearchData() {
    Q_INIT_RESOURCE(block_names);
}
namespace wb {
static QString normalize(const QString &text) {
    QString result;
    for (const auto c : text.normalized(QString::NormalizationForm_D).toLower())
        if (c.isLetterOrNumber())
            result += c;
    return result;
}
QStringList searchKeys(const QString &name, const QString &title) {
    static const QJsonObject data = [] {
        initializeSearchData();
        QFile f(":/translations/pinyin_search.json");
        if (!f.open(QIODevice::ReadOnly))
            return QJsonObject{};
        return QJsonDocument::fromJson(f.readAll()).object();
    }();
    QStringList keys{normalize(title), normalize(name.section(':', -1))};
    const auto phrase = data["titles"].toObject()[title].toArray();
    if (!phrase.isEmpty()) {
        for (const auto &p : phrase)
            keys << normalize(p.toString());
    } else {
        QString pinyin, initials;
        const auto chars = data["characters"].toObject();
        for (const auto c : title) {
            auto syllable = chars.value(QString(c)).toString(QString(c));
            pinyin += syllable;
            initials += syllable.left(1);
        }
        keys << normalize(pinyin) << normalize(initials);
    }
    keys.removeDuplicates();
    return keys;
}
int searchScore(const QStringList &keys, const QString &query) {
    if (query.trimmed().isEmpty())
        return 0;
    if (query.size() > 128)
        return -1;
    const auto tokens =
        query.toLower().replace("minecraft:", "").split(QRegularExpression("[\\s_]+"), Qt::SkipEmptyParts);
    int total = 0;
    for (const auto &raw : tokens) {
        const auto token = normalize(raw);
        if (token.isEmpty())
            continue;
        int score = 1000;
        for (const auto &key : keys) {
            if (key == token)
                score = 0;
            else if (key.startsWith(token))
                score = std::min(score, 1);
            else if (key.contains(token))
                score = std::min(score, 2);
            else if (token.size() >= 4 && score > 2) {
                // Approximate substring edit distance: tolerate a typo without requiring
                // the query to cover an entire material name. Short initials stay exact.
                std::vector<int> previous(key.size() + 1, 0), row(key.size() + 1);
                for (int i = 1; i <= token.size(); ++i) {
                    row[0] = i;
                    for (int j = 1; j <= key.size(); ++j)
                        row[j] = std::min({row[j - 1] + 1, previous[j] + 1,
                                           previous[j - 1] + (token[i - 1] != key[j - 1])});
                    previous.swap(row);
                }
                const int distance = *std::min_element(previous.begin(), previous.end());
                if (distance <= (token.size() >= 7 ? 2 : 1))
                    score = std::min(score, 4 + distance);
            }
        }
        if (score == 1000)
            return -1;
        total += score;
    }
    return total;
}
} // namespace wb
