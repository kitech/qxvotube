#pragma once
#include "common/compat.h"

namespace qxv {

// 本地兜底数据集的一条标题(静态编译进二进制,无外部文件/key 依赖)
struct NfTitle {
    QString id, title, type, year, genres, rating, duration, country, desc;
    NfTitle() {}
    NfTitle(const QString& id_, const QString& title_, const QString& type_,
            const QString& year_, const QString& genres_, const QString& rating_,
            const QString& duration_, const QString& country_, const QString& desc_)
        : id(id_), title(title_), type(type_), year(year_), genres(genres_),
          rating(rating_), duration(duration_), country(country_), desc(desc_) {}
};

// 精选标题集(覆盖主要 genre);无 key/在线失败时的兜底
const QList36<NfTitle>& netflixLocalTitles();

// 从本地数据集派生 genre 列表(用于 categories 兜底)
QStringList netflixLocalGenres();

// 按本地 id 查找(找不到返回 false)
bool netflixFindLocal(const QString& id, NfTitle* out);

} // namespace qxv
