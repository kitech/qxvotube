#include "netflix_data.h"

namespace qxv {

const QList36<NfTitle>& netflixLocalTitles()
{
    // 函数内 static:规避跨 TU 全局构造顺序;Qt3/Qt4/Qt6 同码
    static QList36<NfTitle> s;
    if (!s.isEmpty()) return s;
    // 字段:id / title / type(movie|series) / year / genres(逗号分隔)
    //      / rating / duration / country / desc
    // 说明:静态精选集,可自行替换为 netflix_titles.csv 全量转换结果;
    //       无封面 URL(本地数据集不提供,UI 显示占位)。id 保持唯一。
    s.append(NfTitle("81215567", "Stranger Things", "series", "2016",
                     "Sci-Fi & Fantasy,Dramas,Horror", "TV-14", "4 Seasons", "US",
                     "When a young boy vanishes, a small town uncovers a mystery."));
    s.append(NfTitle("70143836", "Breaking Bad", "series", "2008",
                     "Dramas,Thrillers", "TV-MA", "5 Seasons", "US",
                     "A chemistry teacher turns to a life of crime."));
    s.append(NfTitle("81225793", "The Crown", "series", "2016",
                     "Dramas,Historical", "TV-MA", "6 Seasons", "UK",
                     "The reign of Queen Elizabeth II."));
    s.append(NfTitle("80180939", "The Witcher", "series", "2019",
                     "Sci-Fi & Fantasy,Action & Adventure", "TV-MA", "3 Seasons", "US",
                     "A mutated monster-hunter seeks his destiny."));
    s.append(NfTitle("81097235", "Wednesday", "series", "2022",
                     "Comedies,Mystery,Horror", "TV-14", "1 Season", "US",
                     "Wednesday Addams investigates a spooky mystery."));
    s.append(NfTitle("70104940", "House of Cards", "series", "2013",
                     "Dramas,Thrillers", "TV-MA", "6 Seasons", "US",
                     "A ruthless politician stops at nothing."));
    s.append(NfTitle("70241466", "Narcos", "series", "2015",
                     "Dramas,Crime", "TV-MA", "3 Seasons", "US",
                     "The rise and fall of a cocaine kingpin."));
    s.append(NfTitle("80177743", "Money Heist", "series", "2017",
                     "Thrillers,Crime,Dramas", "TV-MA", "5 Seasons", "ES",
                     "A criminal mastermind plans the ultimate heist."));
    s.append(NfTitle("81168047", "Squid Game", "series", "2021",
                     "Thrillers,Dramas,Survival", "TV-MA", "3 Seasons", "KR",
                     "Hundreds play deadly children's games for a prize."));
    s.append(NfTitle("80078085", "Dark", "series", "2017",
                     "Sci-Fi & Fantasy,Mystery,Thrillers", "TV-MA", "3 Seasons", "DE",
                     "Time travel unites four families."));
    s.append(NfTitle("70143821", "Fullmetal Alchemist", "series", "2003",
                     "Animation,Action & Adventure", "TV-14", "1 Season", "JP",
                     "Two brothers search for the Philosopher's Stone."));
    s.append(NfTitle("70172145", "The Umbrella Academy", "series", "2019",
                     "Sci-Fi & Fantasy,Action & Adventure,Comedies", "TV-14", "4 Seasons", "US",
                     "Estranged siblings reunite to uncover family secrets."));
    s.append(NfTitle("70143827", "The Shawshank Redemption", "movie", "1994",
                     "Dramas", "R", "2h 22m", "US",
                     "Two imprisoned men bond over a number of years."));
    s.append(NfTitle("70143811", "Inception", "movie", "2010",
                     "Sci-Fi & Fantasy,Action & Adventure,Thrillers", "PG-13", "2h 28m", "US",
                     "A thief who steals corporate secrets via dream-sharing."));
    s.append(NfTitle("70002388", "The Dark Knight", "movie", "2008",
                     "Action & Adventure,Crime,Dramas", "PG-13", "2h 32m", "US",
                     "Batman faces the Joker's reign of chaos."));
    s.append(NfTitle("78906556", "Parasite", "movie", "2019",
                     "Thrillers,Comedies,Dramas", "R", "2h 12m", "KR",
                     "Greed and class discrimination threaten a family."));
    s.append(NfTitle("81215566", "Bird Box", "movie", "2018",
                     "Thrillers,Horror,Sci-Fi & Fantasy", "R", "2h 4m", "US",
                     "Survivors navigate a world of unseen monsters."));
    s.append(NfTitle("80101614", "The Irishman", "movie", "2019",
                     "Dramas,Crime", "R", "3h 29m", "US",
                     "Hitman Frank Sheeran looks back at his life."));
    s.append(NfTitle("80657709", "Roma", "movie", "2018",
                     "Dramas", "R", "2h 15m", "MX",
                     "A year in the life of a domestic worker."));
    s.append(NfTitle("80221214", "Extraction", "movie", "2020",
                     "Action & Adventure,Thrillers", "R", "1h 57m", "US",
                     "A hardened mercenary rescues a kidnapped boy."));
    s.append(NfTitle("70177772", "Spirited Away", "movie", "2001",
                     "Animation,Sci-Fi & Fantasy,Kids & Family", "PG", "2h 5m", "JP",
                     "A young girl wanders into a world of spirits."));
    s.append(NfTitle("70166169", "The Social Network", "movie", "2010",
                     "Dramas", "PG-13", "2h", "US",
                     "The story of Facebook's founding and lawsuits."));
    return s;
}

QStringList netflixLocalGenres()
{
    QStringList out;
    const QList36<NfTitle>& all = netflixLocalTitles();
    for (int i = 0; i < all.size(); ++i) {
        const QStringList gs = qSplit(all[i].genres, qFromUtf8(","));
        for (int j = 0; j < gs.size(); ++j) {
            const QString g = gs[j];
            if (!g.isEmpty() && !out.contains(g))
                out.append(g);
        }
    }
    out.sort();
    return out;
}

bool netflixFindLocal(const QString& id, NfTitle* out)
{
    const QList36<NfTitle>& all = netflixLocalTitles();
    for (int i = 0; i < all.size(); ++i)
        if (all[i].id == id) { if (out) *out = all[i]; return true; }
    return false;
}

} // namespace qxv
