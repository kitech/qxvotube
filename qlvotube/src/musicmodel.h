#ifndef MUSICMODEL_H
#define MUSICMODEL_H

#include "compat34.h"
#include <vector>

struct Track {
    int id;
    QString title;
    QString artist;
    QString album;
    int durationMs;
    QString coverSeed;
    QString url;   /* playable URI (empty = not playable yet) */
    /* 远端歌曲：ncmId>0 表示来自线上源（预留） */
    qint64 ncmId;
    qint64 albumId;
    QString coverUrl;
    bool playable;
};

struct Playlist {
    int id;
    QString name;
    QString coverSeed;
    std::vector<Track> tracks;
    /* 远端歌单元数据（id>0 时该歌单来自网络） */
    qint64 ncmId;
    QString creator;
    QString coverUrl;
    int trackCount;
};

inline const Playlist* findPlaylist(const std::vector<Playlist>& lib, int id)
{
    for (size_t i = 0; i < lib.size(); ++i)
        if (lib[i].id == id) return &lib[i];
    return 0;
}

inline Track makeTrack(int id, const char* title, const char* artist,
                       const char* album, int secs, const char* seed,
                       const char* url = 0)
{
    Track t;
    t.id = id;
    t.title = qFromUtf8(title);
    t.artist = qFromUtf8(artist);
    t.album = qFromUtf8(album);
    t.durationMs = secs * 1000;
    t.coverSeed = QString::fromUtf8(seed);
    if (url) t.url = qFromUtf8(url);
    t.ncmId = 0;
    t.albumId = 0;
    t.playable = true;
    return t;
}

/* 远端曲目构造（url 可为空：待 resolve） */
inline Track makeNcmTrack(int id, qint64 ncmId, const QString& title, const QString& artist,
                          const QString& album, int durationMs, const QString& coverUrl,
                          qint64 albumId)
{
    Track t;
    t.id = id;
    t.ncmId = ncmId;
    t.albumId = albumId;
    t.title = title;
    t.artist = artist;
    t.album = album;
    t.durationMs = durationMs;
    t.coverUrl = coverUrl;
    t.playable = true;
    return t;
}

inline std::vector<Playlist> buildTestLibrary()
{
    std::vector<Playlist> lib;

    Playlist p1;
    p1.id = 1;
    p1.name = qFromUtf8("华语流行精选");
    p1.coverSeed = "pl-cpop";
    /* 首条为 cxvideo_selftest --play-probe 生成的 1 秒静音 wav(实～播验证) */
    p1.tracks.push_back(makeTrack(10, "测试音(本地1s)", "cxvideo",
                                  "selftest", 1, "t-10",
                                  "/tmp/opencode/cxvideo_selftest.wav"));
    p1.tracks.push_back(makeTrack(11, "夜空中最亮的星", "逃跑计划", "世界", 252, "t-11"));
    p1.tracks.push_back(makeTrack(12, "起风了", "买辣椒也用券", "起风了", 325, "t-12"));
    p1.tracks.push_back(makeTrack(13, "平凡之路", "朴树", "猎户星座", 304, "t-13"));
    p1.tracks.push_back(makeTrack(14, "成都", "赵雷", "无法长大", 328, "t-14"));
    p1.tracks.push_back(makeTrack(15, "光年之外", "邓紫棋", "光年之外", 235, "t-15"));
    p1.tracks.push_back(makeTrack(16, "句号", "G.E.M.", "启示录", 244, "t-16"));
    p1.ncmId = 0;
    p1.creator = QString();
    p1.coverUrl = QString();
    p1.trackCount = (int)p1.tracks.size();
    lib.push_back(p1);

    Playlist p2;
    p2.id = 2;
    p2.name = qFromUtf8("深夜电台");
    p2.coverSeed = "pl-night";
    p2.tracks.push_back(makeTrack(21, "Yellow", "Coldplay", "Parachutes", 269, "t-21"));
    p2.tracks.push_back(makeTrack(22, "Fix You", "Coldplay", "X&Y", 297, "t-22"));
    p2.tracks.push_back(makeTrack(23, "Creep", "Radiohead", "Pablo Honey", 238, "t-23"));
    p2.tracks.push_back(makeTrack(24, "Nightcall", "Kavinsky", "OutRun", 258, "t-24"));
    p2.tracks.push_back(makeTrack(25, "Blinding Lights", "The Weeknd", "After Hours", 200, "t-25"));
    p2.ncmId = 0;
    p2.creator = QString();
    p2.coverUrl = QString();
    p2.trackCount = (int)p2.tracks.size();
    lib.push_back(p2);

    Playlist p3;
    p3.id = 3;
    p3.name = qFromUtf8("我喜欢的音乐");
    p3.coverSeed = "pl-like";
    p3.tracks.push_back(makeTrack(31, "晴天", "周杰伦", "叶惠美", 269, "t-31"));
    p3.tracks.push_back(makeTrack(32, "七里香", "周杰伦", "七里香", 299, "t-32"));
    p3.tracks.push_back(makeTrack(33, "稻香", "周杰伦", "魔杰座", 223, "t-33"));
    p3.tracks.push_back(makeTrack(34, "青花瓷", "周杰伦", "我很忙", 239, "t-34"));
    p3.ncmId = 0;
    p3.creator = QString();
    p3.coverUrl = QString();
    p3.trackCount = (int)p3.tracks.size();
    lib.push_back(p3);

    Playlist p4;
    p4.id = 4;
    p4.name = qFromUtf8("欧美热歌");
    p4.coverSeed = "pl-west";
    p4.tracks.push_back(makeTrack(41, "Shape of You", "Ed Sheeran", "Divide", 233, "t-41"));
    p4.tracks.push_back(makeTrack(42, "Someone Like You", "Adele", "21", 285, "t-42"));
    p4.tracks.push_back(makeTrack(43, "Counting Stars", "OneRepublic", "Native", 257, "t-43"));
    p4.tracks.push_back(makeTrack(44, "Faded", "Alan Walker", "Different World", 212, "t-44"));
    p4.tracks.push_back(makeTrack(45, "Stay", "The Kid LAROI & Justin Bieber", "F*CK LOVE 3", 141, "t-45"));
    p4.ncmId = 0;
    p4.creator = QString();
    p4.coverUrl = QString();
    p4.trackCount = (int)p4.tracks.size();
    lib.push_back(p4);

    return lib;
}

#endif // MUSICMODEL_H
