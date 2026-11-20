#pragma once
#include "common/compat.h"

namespace qxv {

// 本地补充数据集的一条标题(静态编译进二进制;在线成功时合并补充,不单独作兜底)
struct NfTitle {
    QString id, title, type, year, genres, rating, duration, country, desc;
    NfTitle() {}
    NfTitle(const QString& id_, const QString& title_, const QString& type_,
            const QString& year_, const QString& genres_, const QString& rating_,
            const QString& duration_, const QString& country_, const QString& desc_)
        : id(id_), title(title_), type(type_), year(year_), genres(genres_),
          rating(rating_), duration(duration_), country(country_), desc(desc_) {}
};

// 精选标题集(~400 条,来自公开 netflix_titles.csv);在线成功后按 id 去重补充
const QList36<NfTitle>& netflixLocalTitles();

// 从本地数据集派生 genre 列表(与在线 genres 合并用)
QStringList netflixLocalGenres();

// 按本地 id 查找(找不到返回 false)
bool netflixFindLocal(const QString& id, NfTitle* out);

} // namespace qxv
