#include "netflix_data.h"

namespace qxv {

const QList36<NfTitle>& netflixLocalTitles()
{
    // 函数内 static:规避跨 TU 全局构造顺序;Qt3/Qt4/Qt6 同码
    static QList36<NfTitle> s;
    if (!s.isEmpty()) return s;
    // 字段:id / title / type(movie|series) / year / genres(逗号分隔)
    //      / rating / duration / country / desc
    // 来源:公开 netflix_titles.csv(Tidytuesday 2021)前 400 条真实条目;
    //       id 用 show_id(s1..s400),与在线 nfid 数字 id 不冲突,合并时按 id 去重。
    //       无封面 URL(本地补充集不提供,UI 显示 identicon 占位)。
    s.append(NfTitle("s1", "3%", "series", "2020",
                     "International TV Shows,TV Dramas,TV Sci-Fi & Fantasy", "TV-MA", "4 Seasons", "Brazil",
                     "In a future where the elite inhabit an island paradise far from the crowded slums, you get one chance to join the 3% saved from squalor."));
    s.append(NfTitle("s2", "7:19", "movie", "2016",
                     "Dramas,International Movies", "TV-MA", "93 min", "Mexico",
                     "After a devastating earthquake hits Mexico City, trapped survivors from all walks of life wait to be rescued while trying desperately to stay alive."));
    s.append(NfTitle("s3", "23:59", "movie", "2011",
                     "Horror Movies,International Movies", "R", "78 min", "Singapore",
                     "When an army recruit is found dead, his fellow soldiers are forced to confront a terrifying secret that's haunting their jungle island training camp."));
    s.append(NfTitle("s4", "9", "movie", "2009",
                     "Action & Adventure,Independent Movies,Sci-Fi & Fantasy", "PG-13", "80 min", "United States",
                     "In a postapocalyptic world, rag-doll robots hide in fear from dangerous machines out to exterminate them, until a brave newcomer joins the group."));
    s.append(NfTitle("s5", "21", "movie", "2008",
                     "Dramas", "PG-13", "123 min", "United States",
                     "A brilliant group of students become card-counting experts with the intent of swindling millions out of Las Vegas casinos by playing blackjack."));
    s.append(NfTitle("s6", "46", "series", "2016",
                     "International TV Shows,TV Dramas,TV Mysteries", "TV-MA", "1 Season", "Turkey",
                     "A genetics professor experiments with a treatment for his comatose sister that blends medical and shamanic cures, but unlocks a shocking side effect."));
    s.append(NfTitle("s7", "122", "movie", "2019",
                     "Horror Movies,International Movies", "TV-MA", "95 min", "Egypt",
                     "After an awful accident, a couple admitted to a grisly hospital are separated and must find each other to escape — before death finds them."));
    s.append(NfTitle("s8", "187", "movie", "1997",
                     "Dramas", "R", "119 min", "United States",
                     "After one of his high school students attacks him, dedicated teacher Trevor Garfield grows weary of the gang warfare in the New York City school system and moves to California to teach there, thinking it must be a less hostile environment."));
    s.append(NfTitle("s9", "706", "movie", "2019",
                     "Horror Movies,International Movies", "TV-14", "118 min", "India",
                     "When a doctor goes missing, his psychiatrist wife treats the bizarre medical condition of a psychic patient, who knows much more than he's leading on."));
    s.append(NfTitle("s10", "1920", "movie", "2008",
                     "Horror Movies,International Movies,Thrillers", "TV-MA", "143 min", "India",
                     "An architect and his wife move into a castle that is slated to become a luxury hotel. But something inside is determined to stop the renovation."));
    s.append(NfTitle("s11", "1922", "movie", "2017",
                     "Dramas,Thrillers", "TV-MA", "103 min", "United States",
                     "A farmer pens a confession admitting to his wife's murder, but her death is just the beginning of a macabre tale. Based on Stephen King's novella."));
    s.append(NfTitle("s12", "1983", "series", "2018",
                     "Crime TV Shows,International TV Shows,TV Dramas", "TV-MA", "1 Season", "Poland, United States",
                     "In this dark alt-history thriller, a naïve law student and a world-weary detective uncover a conspiracy that has tyrannized Poland for decades."));
    s.append(NfTitle("s13", "1994", "series", "2019",
                     "Crime TV Shows,Docuseries,International TV Shows", "TV-MA", "1 Season", "Mexico",
                     "Archival video and new interviews examine Mexican politics in 1994, a year marked by the rise of the EZLN and the assassination of Luis Donaldo Colosio."));
    s.append(NfTitle("s14", "2,215", "movie", "2018",
                     "Documentaries,International Movies,Sports Movies", "TV-MA", "89 min", "Thailand",
                     "This intimate documentary follows rock star Artiwara Kongmalai on his historic, 2,215-kilometer charity run across Thailand in 2017."));
    s.append(NfTitle("s15", "3022", "movie", "2019",
                     "Independent Movies,Sci-Fi & Fantasy,Thrillers", "R", "91 min", "United States",
                     "Stranded when the Earth is suddenly destroyed in a mysterious cataclysm, the astronauts aboard a marooned space station slowly lose their minds."));
    s.append(NfTitle("s16", "Oct-01", "movie", "2014",
                     "Dramas,International Movies,Thrillers", "TV-14", "149 min", "Nigeria",
                     "Against the backdrop of Nigeria's looming independence from Britain, detective Danladi Waziri races to capture a killer terrorizing local women."));
    s.append(NfTitle("s17", "Feb-09", "series", "2018",
                     "International TV Shows,TV Dramas", "TV-14", "1 Season", "",
                     "As a psychology professor faces Alzheimer's, his daughter and her three close female friends experience romance, marriage, heartbreak and tragedy."));
    s.append(NfTitle("s18", "22-Jul", "movie", "2018",
                     "Dramas,Thrillers", "R", "144 min", "Norway, Iceland, United States",
                     "After devastating terror attacks in Norway, a young survivor, grieving families and the country rally for justice and healing. Based on a true story."));
    s.append(NfTitle("s19", "15-Aug", "movie", "2019",
                     "Comedies,Dramas,Independent Movies", "TV-14", "124 min", "India",
                     "On India's Independence Day, a zany mishap in a Mumbai chawl disrupts a young love story while compelling the residents to unite in aid of a little boy."));
    s.append(NfTitle("s20", "'89", "movie", "2017",
                     "Sports Movies", "TV-PG", "87 min", "United Kingdom",
                     "Mixing old footage with interviews, this is the story of Arsenal's improbable win versus Liverpool in the final moments of the 1989 championship game."));
    s.append(NfTitle("s21", "​​Kuch Bheege Alfaaz", "movie", "2018",
                     "Dramas,Independent Movies,International Movies", "TV-14", "110 min", "India",
                     "After accidentally connecting over the Internet, two strangers form a tight friendship – not knowing they already share a bond."));
    s.append(NfTitle("s22", "​Goli Soda 2", "movie", "2018",
                     "Action & Adventure,Dramas,International Movies", "TV-14", "128 min", "India",
                     "A taxi driver, a gangster and an athlete struggle to better their lives despite obstacles like crooked politicians, evil dons and caste barriers."));
    s.append(NfTitle("s23", "​Maj Rati ​​Keteki", "movie", "2017",
                     "Dramas,International Movies", "TV-14", "117 min", "India",
                     "A successful writer returns to the town that launched his career, encountering people who spark nostalgic, often painfully illuminating flashbacks."));
    s.append(NfTitle("s24", "​Mayurakshi", "movie", "2017",
                     "Dramas,International Movies", "TV-14", "100 min", "India",
                     "When a middle-aged divorcee returns to Kolkata to visit his ailing father, long-buried memories resurface, bringing new discoveries with them."));
    s.append(NfTitle("s25", "​SAINT SEIYA: Knights of the Zodiac", "series", "2020",
                     "Anime Series,International TV Shows", "TV-14", "2 Seasons", "Japan",
                     "Seiya and the Knights of the Zodiac rise again to protect the reincarnation of the goddess Athena, but a dark prophecy hangs over them all."));
    s.append(NfTitle("s26", "(T)ERROR", "movie", "2015",
                     "Documentaries", "NR", "84 min", "United States",
                     "This real-life look at FBI counterterrorism operations features access to both sides of a sting: the government informant and the radicalized target."));
    s.append(NfTitle("s27", "(Un)Well", "series", "2020",
                     "Reality TV", "TV-MA", "1 Season", "United States",
                     "This docuseries takes a deep dive into the lucrative wellness industry, which touts health and healing. But do the products live up to the promises?"));
    s.append(NfTitle("s28", "#Alive", "movie", "2020",
                     "Horror Movies,International Movies,Thrillers", "TV-MA", "99 min", "South Korea",
                     "As a grisly virus rampages a city, a lone man stays locked inside his apartment, digitally cut off from seeking help and desperate to find a way out."));
    s.append(NfTitle("s29", "#AnneFrank - Parallel Stories", "movie", "2019",
                     "Documentaries,International Movies", "TV-14", "95 min", "Italy",
                     "Through her diary, Anne Frank's story is retold alongside those of five Holocaust survivors in this poignant documentary from Oscar winner Helen Mirren."));
    s.append(NfTitle("s30", "#blackAF", "series", "2020",
                     "TV Comedies", "TV-MA", "1 Season", "United States",
                     "Kenya Barris and his family navigate relationships, race and culture while grappling with their newfound success in this comedy series."));
    s.append(NfTitle("s31", "#cats_the_mewvie", "movie", "2020",
                     "Documentaries,International Movies", "TV-14", "90 min", "Canada",
                     "This pawesome documentary explores how our feline friends became online icons, from the earliest text memes to the rise of celebrity cat influencers."));
    s.append(NfTitle("s32", "#FriendButMarried", "movie", "2018",
                     "Dramas,International Movies,Romantic Movies", "TV-G", "102 min", "Indonesia",
                     "Pining for his high school crush for years, a young man puts up his best efforts to move out of the friend zone until she reveals she's getting married."));
    s.append(NfTitle("s33", "#FriendButMarried 2", "movie", "2020",
                     "Dramas,International Movies,Romantic Movies", "TV-G", "104 min", "Indonesia",
                     "As Ayu and Ditto finally transition from best friends to newlyweds, a quick pregnancy creates uncertainty for the future of their young marriage."));
    s.append(NfTitle("s34", "#realityhigh", "movie", "2017",
                     "Comedies", "TV-14", "99 min", "United States",
                     "When nerdy high schooler Dani finally attracts the interest of her longtime crush, she lands in the cross hairs of his ex, a social media celebrity."));
    s.append(NfTitle("s35", "#Roxy", "movie", "2018",
                     "Comedies,Romantic Movies", "TV-14", "105 min", "Canada",
                     "A teenage hacker with a huge nose helps a cool kid woo a girl that he’s secretly in love with."));
    s.append(NfTitle("s36", "#Rucker50", "movie", "2016",
                     "Documentaries,Sports Movies", "TV-PG", "56 min", "United States",
                     "This documentary celebrates the 50th anniversary of the Harlem sports program that has inspired countless city kids to become pro basketball players."));
    s.append(NfTitle("s37", "#Selfie", "movie", "2014",
                     "Comedies,Dramas,International Movies", "TV-MA", "125 min", "Romania",
                     "Two days before their final exams, three teen girls make a seaside getaway to end their adolescence with a bang."));
    s.append(NfTitle("s38", "#Selfie 69", "movie", "2016",
                     "Comedies,Dramas,International Movies", "TV-MA", "119 min", "Romania",
                     "After a painful breakup, a trio of party-loving friends makes a bet: who can get married in three days."));
    s.append(NfTitle("s39", "แผนร้ายนายเจ้าเล่ห์", "series", "2016",
                     "International TV Shows,Romantic TV Shows,TV Comedies", "TV-14", "1 Season", "",
                     "When two brothers fall for two sisters, they quickly realize the age differences between them are too big to ignore."));
    s.append(NfTitle("s40", "¡Ay, mi madre!", "movie", "2019",
                     "Comedies,International Movies", "TV-MA", "81 min", "Spain",
                     "When her estranged mother suddenly dies, a woman must follow the quirky instructions laid out in the will in order to collect an important inheritance."));
    s.append(NfTitle("s41", "Çarsi Pazar", "movie", "2015",
                     "Comedies,International Movies", "TV-14", "97 min", "Turkey",
                     "The slacker owner of a public bath house rallies his community to save it when a big developer comes to town to close it down and open a new mall."));
    s.append(NfTitle("s42", "Ég man þig", "movie", "2017",
                     "Horror Movies,International Movies", "TV-MA", "106 min", "Iceland",
                     "Young urbanites renovating a rundown house, and a psychiatrist grieving his son's disappearance, are connected to a supernatural, decades-old secret."));
    s.append(NfTitle("s43", "Çok Filim Hareketler Bunlar", "movie", "2010",
                     "Comedies,International Movies", "TV-MA", "99 min", "Turkey",
                     "Vignettes of the summer holidays follow vacationers as they battle mosquitoes, suffer ruined plans and otherwise hit snags in their precious time off."));
    s.append(NfTitle("s44", "Òlòtūré", "movie", "2020",
                     "Dramas,International Movies", "TV-MA", "107 min", "Nigeria",
                     "In Lagos, a journalist goes undercover as a prostitute to expose human trafficking. What she finds is a world of exploited women and ruthless violence."));
    s.append(NfTitle("s45", "Æon Flux", "movie", "2005",
                     "Action & Adventure,Sci-Fi & Fantasy", "PG-13", "93 min", "United States",
                     "Aiming to hasten an uprising, the leader of an underground rebellion dispatches acrobatic assassin Aeon Flux to eliminate the government's top leader."));
    s.append(NfTitle("s46", "Şubat", "series", "2013",
                     "Crime TV Shows,International TV Shows,TV Dramas", "TV-MA", "1 Season", "Turkey",
                     "An orphan subjected to tests that gave him superpowers is rescued and raised on Istanbul's streets, where he falls for a reporter linked to his past."));
    s.append(NfTitle("s47", "1 Chance 2 Dance", "movie", "2014",
                     "Dramas,Romantic Movies", "TV-PG", "89 min", "United States",
                     "When an aspiring dancer is uprooted during her senior year of high school, she finds herself torn between two boys – and with one shot at her dream."));
    s.append(NfTitle("s48", "1 Mile to You", "movie", "2017",
                     "Dramas,Romantic Movies,Sports Movies", "TV-14", "105 min", "United States",
                     "After escaping the bus accident that killed his girlfriend, a high school student channels his grief into running, with the help of a new coach."));
    s.append(NfTitle("s49", "10 Days in Sun City", "movie", "2017",
                     "Comedies,International Movies,Romantic Movies", "TV-14", "87 min", "South Africa, Nigeria",
                     "After his girlfriend wins the Miss Nigeria pageant, a young man faces unexpected competition of his own when he joins her on a campaign in South Africa."));
    s.append(NfTitle("s50", "10 jours en or", "movie", "2012",
                     "Comedies,Dramas,International Movies", "TV-14", "97 min", "France",
                     "When a carefree bachelor is unexpectedly left in charge of a young boy, the two embark on a road trip that will change both of their lives."));
    s.append(NfTitle("s51", "10,000 B.C.", "movie", "2008",
                     "Action & Adventure", "PG-13", "109 min", "United States, South Africa",
                     "Fierce mammoth hunter D'Leh sets out on an impossible journey to rescue the woman he loves from a vicious warlord and save the people of his village."));
    s.append(NfTitle("s52", "100 Days My Prince", "series", "2018",
                     "International TV Shows,Romantic TV Shows,TV Dramas", "TV-14", "1 Season", "South Korea",
                     "Upon losing his memory, a crown prince encounters a commoner’s life and experiences unforgettable love as the husband to Joseon’s oldest bachelorette."));
    s.append(NfTitle("s53", "100 Days Of Solitude", "movie", "2018",
                     "Documentaries,International Movies", "TV-MA", "93 min", "Spain",
                     "Spanish photographer José Díaz spends 100 days living alone on a remote mountain, connecting to nature and documenting the beauty of his surroundings."));
    s.append(NfTitle("s54", "100 Humans", "series", "2020",
                     "Docuseries,Science & Nature TV", "TV-14", "1 Season", "United States",
                     "One hundred hardy souls from diverse backgrounds participate in playful experiments exploring age, sex, happiness and other aspects of being human."));
    s.append(NfTitle("s55", "100 Meters", "movie", "2016",
                     "Dramas,International Movies,Sports Movies", "TV-MA", "109 min", "Portugal, Spain",
                     "A man who is diagnosed with multiple sclerosis responds by training for an Ironman triathlon, with his cranky father-in-law's help."));
    s.append(NfTitle("s56", "100 Things to do Before High School", "movie", "2014",
                     "Movies", "TV-Y", "44 min", "United States",
                     "Led by seventh-grader C.J., three students who have been warned about the dangers of high school decide to make the best of their middle-school years."));
    s.append(NfTitle("s57", "100 Years: One Woman's Fight for Justice", "movie", "2016",
                     "Documentaries", "TV-14", "75 min", "United States",
                     "This documentary chronicles Elouise Cobell's long fight against the U.S. government for the gross mismanagement of mineral-rich Native American land."));
    s.append(NfTitle("s58", "100% Halal", "movie", "2020",
                     "Dramas,International Movies", "TV-14", "101 min", "Indonesia",
                     "After high school, a young woman marries the man of her father's choice but soon faces the possibility that her religion considers the union invalid."));
    s.append(NfTitle("s59", "100% Hotter", "series", "2017",
                     "British TV Shows,International TV Shows,Reality TV", "TV-14", "1 Season", "United Kingdom",
                     "A stylist, a hair designer and a makeup artist team up to give Britain's biggest fashion disasters some much-needed makeunders."));
    s.append(NfTitle("s60", "1000 Rupee Note", "movie", "2014",
                     "Dramas,International Movies", "TV-14", "89 min", "India",
                     "After randomly receiving a handsome political bribe, a sweet, poor elderly woman decides to treat herself a shopping spree, which doesn't go smoothly."));
    s.append(NfTitle("s61", "12 ROUND GUN", "movie", "2017",
                     "Dramas,Independent Movies,Sports Movies", "TV-MA", "90 min", "United States",
                     "Dealing with personal demons and the death of his son, a prizefighter attempts a return to the ring by challenging his rival to a 12-round rematch."));
    s.append(NfTitle("s62", "12 Years Promise", "series", "2014",
                     "International TV Shows,Korean TV Shows,Romantic TV Shows", "TV-14", "1 Season", "South Korea",
                     "A pregnant teen is forced by her family to leave her boyfriend and assume a new identity in America, but 12 years later, the couple reunites in Korea."));
    s.append(NfTitle("s63", "13 Cameras", "movie", "2015",
                     "Horror Movies,Independent Movies,Thrillers", "NR", "90 min", "United States",
                     "Young parents-to-be Claire and Ryan move into a suburban rental home, unaware that someone is secretly watching their every move via hidden cameras."));
    s.append(NfTitle("s64", "13 Reasons Why", "series", "2020",
                     "Crime TV Shows,TV Dramas,TV Mysteries", "TV-MA", "4 Seasons", "United States",
                     "After a teenage girl's perplexing suicide, a classmate receives a series of tapes that unravel the mystery of her tragic choice."));
    s.append(NfTitle("s65", "13 Reasons Why: Beyond the Reasons", "series", "2019",
                     "Crime TV Shows,Docuseries", "TV-MA", "3 Seasons", "United States",
                     "Cast members, writers, producers and mental health professionals discuss some of the difficult issues and themes explored in \"13 Reasons Why.\""));
    s.append(NfTitle("s66", "13 Sins", "movie", "2014",
                     "Horror Movies,Thrillers", "R", "93 min", "United States",
                     "A man agrees to appear on a game show with a $6 million prize. But as the challenges become more extreme, he realizes he's made a grave mistake."));
    s.append(NfTitle("s67", "13TH", "movie", "2016",
                     "Documentaries", "TV-MA", "101 min", "United States",
                     "In this thought-provoking documentary, scholars, activists and politicians analyze the criminalization of African Americans and the U.S. prison boom."));
    s.append(NfTitle("s68", "13TH: A Conversation with Oprah Winfrey & Ava DuVernay", "movie", "2017",
                     "Movies", "", "37 min", "",
                     "Oprah Winfrey sits down with director Ava DuVernay to discuss her Oscar-nominated film, historical cycles of oppression and the broken prison system."));
    s.append(NfTitle("s69", "14 Blades", "movie", "2010",
                     "Action & Adventure,International Movies", "R", "113 min", "Hong Kong, China, Singapore",
                     "In the age of the Ming Dynasty, Quinglong is the best of the Jinyiwei, an elite assassin squad made up of highly trained former street urchins. When evil eunuch Jia unseats the emperor, Quinglong is called to action but is quickly betrayed."));
    s.append(NfTitle("s70", "14 Cameras", "movie", "2018",
                     "Horror Movies,Thrillers", "TV-MA", "89 min", "United States",
                     "Upping the “13 Cameras” ante, this sequel finds a family renting a vacation house that’s been customized to violate their privacy."));
    s.append(NfTitle("s71", "14 Minutes from Earth", "movie", "2016",
                     "Documentaries", "TV-MA", "84 min", "United States",
                     "A Google executive boldly attempts a death-defying mission to travel to space and free-fall back to Earth without a rocket."));
    s.append(NfTitle("s72", "16 Blocks", "movie", "2006",
                     "Action & Adventure", "PG-13", "102 min", "United States, Germany",
                     "Tasked with escorting a prosecution witness to court, an aging cop gears up for the 16-block trek – but they'll be lucky to make it there alive."));
    s.append(NfTitle("s73", "17 Again", "movie", "2009",
                     "Comedies", "PG-13", "102 min", "United States",
                     "Nearing a midlife crisis, thirty-something Mike wishes for a \"do-over\" – and that's exactly what he gets when he wakes up to find he's 17 again."));
    s.append(NfTitle("s74", "18 Presents", "movie", "2020",
                     "Dramas,Independent Movies,International Movies", "TV-MA", "114 min", "Italy",
                     "A pregnant mother with terminal cancer leaves behind 18 sentimental gifts for her unborn daughter to receive every birthday until she reaches womanhood."));
    s.append(NfTitle("s75", "1898: Our Last Men in the Philippines", "movie", "2016",
                     "Dramas,International Movies", "R", "130 min", "Spain",
                     "While Spain relinquishes its last colonies, a battle-fatigued outpost engages in a long, brutal and sometimes bizarre clash with Filipino insurgents."));
    s.append(NfTitle("s76", "1BR", "movie", "2019",
                     "Horror Movies,Independent Movies,Thrillers", "TV-MA", "90 min", "United States",
                     "Seeking her independence, a young woman moves to Los Angeles and settles into a cozy apartment complex with a disturbing sense of community."));
    s.append(NfTitle("s77", "1st Summoning", "movie", "2018",
                     "Horror Movies,Independent Movies", "TV-MA", "94 min", "United States",
                     "Student filmmakers uncover occult rituals tied to an abandoned warehouse, then gradually realize the horror is closer than they think."));
    s.append(NfTitle("s78", "2 Alone in Paris", "movie", "2008",
                     "Comedies,International Movies", "TV-MA", "97 min", "France",
                     "A bumbling Paris policeman is doggedly determined to capture the master thief that repeatedly eludes him, even when they're the last two men on Earth."));
    s.append(NfTitle("s79", "2 States", "movie", "2014",
                     "Comedies,Dramas,International Movies", "TV-PG", "143 min", "India",
                     "Graduate students Krish and Ananya hope to win their parents' approval before they marry, but the two families clash over their cultural differences."));
    s.append(NfTitle("s80", "20 Feet From Stardom", "movie", "2013",
                     "Documentaries,Music & Musicals", "PG-13", "91 min", "United States",
                     "Winner of the 2014 Academy Award for Best Documentary Feature, this film takes a look at the world of backup vocalists and the legends they support."));
    s.append(NfTitle("s81", "20 Minutes", "series", "2013",
                     "Crime TV Shows,International TV Shows,TV Dramas", "TV-MA", "1 Season", "Turkey",
                     "When his wife is convicted of murder, a horrified family man races to prove her innocence while a dogged investigator sets out to uncover the truth."));
    s.append(NfTitle("s82", "2015 Dream Concert", "movie", "2015",
                     "International Movies,Music & Musicals", "TV-PG", "107 min", "South Korea",
                     "The world's biggest K-pop festival marked its 21st year in 2015, with groups such as EXO, 4Minute and SHINee electrifying the Seoul World Cup Stadium."));
    s.append(NfTitle("s83", "2036 Origin Unknown", "movie", "2018",
                     "Sci-Fi & Fantasy", "TV-14", "95 min", "United Kingdom",
                     "Working with an artificial intelligence to investigate the failure of a deadly Mars landing, a mission controller makes an astonishing discovery."));
    s.append(NfTitle("s84", "20th Century Women", "movie", "2016",
                     "Dramas,Independent Movies", "R", "119 min", "United States",
                     "In 1979, single bohemian mom Dorothea, hoping to help her teen son find his place as a man, asks two young women to share their lives with him."));
    s.append(NfTitle("s85", "21 & Over", "movie", "2013",
                     "Comedies", "R", "93 min", "United States",
                     "Jeff's straight-and-narrow life changes abruptly when his buddies take him out for a birthday bash – the night before a crucial med school interview."));
    s.append(NfTitle("s86", "21 Again", "series", "2019",
                     "British TV Shows,Reality TV", "TV-MA", "1 Season", "United Kingdom",
                     "In a social experiment, a group of daughters sends their mothers, disguised as 21-year-olds, into the world to experience life as a member of Gen Z."));
    s.append(NfTitle("s87", "21 Sarfarosh: Saragarhi 1897", "series", "2018",
                     "International TV Shows,TV Dramas", "TV-14", "1 Season", "India",
                     "In one of history's greatest last stands, a battalion of 21 Sikh soldiers fights to defend their outpost from attack by over 10,000 Afghans."));
    s.append(NfTitle("s88", "21 Thunder", "series", "2017",
                     "Crime TV Shows,International TV Shows,TV Dramas", "TV-MA", "1 Season", "Canada",
                     "Players and coaches for a Montreal soccer team chase dreams of stardom while their personal lives erupt with love, fear, passion and violence."));
    s.append(NfTitle("s89", "2307: Winter's Dream", "movie", "2016",
                     "Action & Adventure,Independent Movies,Sci-Fi & Fantasy", "TV-MA", "101 min", "United States",
                     "In the frozen tundra of a futuristic Arizona where humans have been forced underground, a soldier hunts the bioengineered leader of a rebellion."));
    s.append(NfTitle("s90", "24 Hours to Live", "movie", "2017",
                     "Action & Adventure", "R", "93 min", "South Africa, China, United States",
                     "Revived by an experimental procedure, a hit man gets to live an extra 24 hours – which he uses to avenge his dead wife and child and redeem himself."));
    s.append(NfTitle("s91", "25 Kille", "movie", "2016",
                     "Action & Adventure,Dramas,International Movies", "TV-14", "140 min", "India",
                     "Four brothers learn that they have inherited ancestral land, but must fight to claim it when a greedy feudal lord threatens to take it over."));
    s.append(NfTitle("s92", "26 Years", "movie", "2012",
                     "Dramas,International Movies,Thrillers", "TV-MA", "135 min", "South Korea",
                     "Twenty-six years after the 1980 massacre at Gwangju, South Korea, three relatives of the victims come together to avenge the infamous orchestrator."));
    s.append(NfTitle("s93", "27, el club de los malditos", "movie", "2018",
                     "Action & Adventure,Comedies,International Movies", "TV-MA", "82 min", "Argentina",
                     "After a musician dies under suspicious circumstances, a hard-drinking detective and a music fan investigate why rock stars often die at the age of 27."));
    s.append(NfTitle("s94", "27: Gone Too Soon", "movie", "2017",
                     "Documentaries", "TV-MA", "70 min", "United Kingdom",
                     "Explore the circumstances surrounding the tragic deaths at 27 of Jimi Hendrix, Jim Morrison, Brian Jones, Janis Joplin, Kurt Cobain and Amy Winehouse."));
    s.append(NfTitle("s95", "28 Days", "movie", "2000",
                     "Comedies,Dramas", "PG-13", "104 min", "United States",
                     "After her drunken antics result in property damage, an alcoholic journalist enters rehab – and soon meets a fellow resident who changes her outlook."));
    s.append(NfTitle("s96", "28 Moons", "series", "2016",
                     "International TV Shows,Korean TV Shows,Romantic TV Shows", "TV-PG", "1 Season", "South Korea",
                     "When her fiancé acts strangely upon receiving a mysterious invitation, a florist sets out to find out the truth behind the tiny scrap of paper."));
    s.append(NfTitle("s97", "3 Days to Kill", "movie", "2014",
                     "Action & Adventure", "PG-13", "117 min", "United States, France, Serbia",
                     "A terminally ill secret agent accepts a risky mission in exchange for an experimental drug that might save him – if he can survive its side effects."));
    s.append(NfTitle("s98", "3 Deewarein", "movie", "2003",
                     "Dramas,International Movies,Thrillers", "TV-MA", "121 min", "",
                     "A reporter interviews three convicts on death row for a documentary but as their stories emerge, so do her own true motivations for soliciting them."));
    s.append(NfTitle("s99", "3 Generations", "movie", "2015",
                     "Dramas,LGBTQ Movies", "PG-13", "92 min", "United States",
                     "When teenage Ray begins transitioning from female to male, his single mom and grandmother must cope with the change while tracking down his father."));
    s.append(NfTitle("s100", "3 Heroines", "movie", "2016",
                     "Dramas,International Movies,Sports Movies", "TV-PG", "124 min", "Indonesia",
                     "Three Indonesian women break records by becoming the first of their nation to medal in archery at the Seoul Olympics in the summer of 1988."));
    s.append(NfTitle("s101", "3 Idiots", "movie", "2009",
                     "Comedies,Dramas,International Movies", "PG-13", "164 min", "India",
                     "While attending one of India's premier colleges, three miserable engineering students and best friends struggle to beat the school's draconian system."));
    s.append(NfTitle("s102", "3 Seconds Divorce", "movie", "2018",
                     "Documentaries,Faith & Spirituality", "TV-PG", "53 min", "Canada",
                     "A Muslim women's activist group in India protests against oral divorces, starting a movement to reclaim their religious and constitutional rights."));
    s.append(NfTitle("s103", "3 Türken & ein Baby", "movie", "2015",
                     "Comedies,International Movies", "TV-MA", "93 min", "Germany",
                     "The lives of three dissatisfied brothers running a family bridal shop turn topsy-turvy when one of them has to care for his ex-girlfriend's baby."));
    s.append(NfTitle("s104", "30 Days of Luxury", "movie", "2016",
                     "Comedies,International Movies", "TV-14", "91 min", "Egypt",
                     "With the help of his friends, a man breaks out of prison in hopes of restoring a lively nightclub."));
    s.append(NfTitle("s105", "30 Minutes or Less", "movie", "2011",
                     "Action & Adventure,Comedies", "R", "83 min", "United States",
                     "Two crooks planning a bank heist wind up abducting a pizza delivery driver and force him to commit the robbery — with a strict time limit."));
    s.append(NfTitle("s106", "300 Miles to Heaven", "movie", "1989",
                     "Dramas,International Movies", "TV-14", "93 min", "Denmark, France, Poland",
                     "Hoping to help their dissident parents, two brothers sneak out of Poland and land as refugees in Denmark, where they are prevented from returning home."));
    s.append(NfTitle("s107", "365 Days", "movie", "2020",
                     "Dramas,International Movies,Romantic Movies", "TV-MA", "114 min", "Poland",
                     "A fiery executive in a spiritless relationship falls victim to a dominant mafia boss, who imprisons her and gives her one year to fall in love with him."));
    s.append(NfTitle("s108", "37 Seconds", "movie", "2020",
                     "Dramas,International Movies", "TV-MA", "116 min", "Japan",
                     "Trapped by society and familial obligations, a young manga artist goes on an unconventional journey for sexual freedom and personal liberation."));
    s.append(NfTitle("s109", "3Below: Tales of Arcadia", "series", "2019",
                     "Kids' TV,TV Action & Adventure,TV Sci-Fi & Fantasy", "TV-Y7", "2 Seasons", "United States",
                     "After crash-landing on Earth, two royal teen aliens on the run struggle to blend in with humans as they evade intergalactic bounty hunters."));
    s.append(NfTitle("s110", "40 Sticks", "movie", "2019",
                     "International Movies,Thrillers", "TV-14", "93 min", "Kenya",
                     "When their prison bus crashes in a forest on a rainy night, a group of criminals finds themselves battling wild animals and a mysterious killer."));
    s.append(NfTitle("s111", "42 Grams", "movie", "2017",
                     "Documentaries", "TV-MA", "81 min", "United States",
                     "After launching a successful underground restaurant out of their apartment, an ambitious chef and his wife open an all-consuming upscale eatery."));
    s.append(NfTitle("s112", "44 Cats", "series", "2018",
                     "Kids' TV", "TV-Y7", "2 Seasons", "Italy",
                     "Paw-esome tales abound when singing furry friends Lampo, Milady, Pilou and Meatball band together."));
    s.append(NfTitle("s113", "45 rpm", "series", "2019",
                     "International TV Shows,Romantic TV Shows,Spanish-Language TV Shows", "TV-MA", "1 Season", "Spain",
                     "In 1960s Madrid, music producer Guillermo Rojas launches a rock 'n' roll label with the help of aspiring singer Robert and clever producer Maribel."));
    s.append(NfTitle("s114", "48 Christmas Wishes", "movie", "2017",
                     "Children & Family Movies,Comedies", "TV-G", "84 min", "Canada",
                     "When a small town's letters to Santa accidentally go up in smoke, two elves venture out of the North Pole to retrieve every missing wish."));
    s.append(NfTitle("s115", "4L", "movie", "2019",
                     "Comedies,Dramas,Independent Movies", "TV-MA", "105 min", "Spain",
                     "Hoping to reunite with a dying friend, two longtime pals re-create their desert road trip from Spain to Mali, bringing along his estranged daughter."));
    s.append(NfTitle("s116", "4th Man Out", "movie", "2015",
                     "Comedies,LGBTQ Movies", "NR", "86 min", "United States",
                     "A young mechanic comes out to his extremely straight best friends. Once they get used to the idea, they're determined to help him find the right guy."));
    s.append(NfTitle("s117", "4th Republic", "movie", "2019",
                     "Dramas,International Movies,Thrillers", "TV-MA", "120 min", "Nigeria",
                     "After the election-night murder of her campaign manager at a polling site, a gubernatorial candidate challenges the corrupt incumbent's victory."));
    s.append(NfTitle("s118", "5 Cowok Jagoan", "movie", "2017",
                     "Action & Adventure,Comedies,International Movies", "TV-MA", "106 min", "",
                     "Yanto asks his friends to help save a girlfriend who was kidnapped by a mobster. Silly slapstick and campy crimefighting ensues."));
    s.append(NfTitle("s119", "5 Flights Up", "movie", "2014",
                     "Comedies,Dramas,Independent Movies", "PG-13", "92 min", "United States",
                     "A couple finds unexpected drama when, after 40 years of living in the same Brooklyn walk-up, they attempt to sell their apartment and buy a new one."));
    s.append(NfTitle("s120", "5 Star Christmas", "movie", "2018",
                     "Comedies,International Movies", "TV-14", "96 min", "Italy",
                     "When the Italian prime minister meets his lover and political rival while on an official trip to Budapest during Christmas week, things go topsy-turvy."));
    s.append(NfTitle("s121", "5 to 7", "movie", "2014",
                     "Comedies,Dramas,Romantic Movies", "R", "97 min", "United States",
                     "A young novelist's life is turned upside down when a chance encounter outside a New York hotel leads to an intense affair with a diplomat's wife."));
    s.append(NfTitle("s122", "50 First Dates", "movie", "2004",
                     "Comedies,Romantic Movies", "PG-13", "99 min", "United States",
                     "After falling for a pretty art teacher who has no short-term memory, a marine veterinarian has to win her over again every single day."));
    s.append(NfTitle("s123", "50/50", "movie", "2011",
                     "Comedies,Dramas,Independent Movies", "R", "100 min", "United States",
                     "An otherwise healthy twentysomething has a comically early midlife crisis when he gets slapped with a cancer diagnosis and a 50-50 chance of survival."));
    s.append(NfTitle("s124", "5CM", "movie", "2012",
                     "Dramas,International Movies", "TV-14", "126 min", "Indonesia",
                     "Five friends embark on a mission to climb the highest peak in Java, overcoming obstacles on the way and discovering the true meaning of friendship."));
    s.append(NfTitle("s125", "5Gang", "movie", "2019",
                     "Comedies,International Movies", "TV-14", "96 min", "Romania",
                     "To keep the band together, Selly tries to earn money by making an appearance at the birthday party of a mobster's daughter — until he gets kidnapped."));
    s.append(NfTitle("s126", "6 Balloons", "movie", "2018",
                     "Dramas,Independent Movies", "TV-MA", "75 min", "United States",
                     "A loyal sister struggles to stay afloat while driving her heroin-addicted brother to a detox center and looking after his 2-year-old daughter."));
    s.append(NfTitle("s127", "6 Days", "movie", "2017",
                     "Action & Adventure,Dramas,International Movies", "R", "94 min", "New Zealand, United Kingdom",
                     "When armed gunmen seize the Iranian Embassy in 1980, a tense six-day standoff ensues while elite British soldiers prepare for a dangerous raid."));
    s.append(NfTitle("s128", "6 Underground", "movie", "2019",
                     "Action & Adventure,Dramas", "R", "129 min", "United States",
                     "After faking his death, a tech billionaire recruits a team of international operatives for a bold and bloody mission to take down a brutal dictator."));
    s.append(NfTitle("s129", "6 Years", "movie", "2015",
                     "Dramas,Independent Movies,Romantic Movies", "TV-MA", "80 min", "United States",
                     "As a volatile young couple who have been together for six years approach college graduation, unexpected career opportunities threaten their future."));
    s.append(NfTitle("s130", "6-5=2", "movie", "2014",
                     "Horror Movies,International Movies,Thrillers", "TV-MA", "103 min", "India",
                     "Six friends decide to undertake a grueling mountain trek, only to find that the difficulties in store for them are not merely physical."));
    s.append(NfTitle("s131", "60 Days In", "series", "2019",
                     "Reality TV", "TV-MA", "1 Season", "United States",
                     "Recruited by a sheriff, volunteers infiltrate county prisons to expose corruption and crime from within the system in this docuseries."));
    s.append(NfTitle("s132", "68 Kill", "movie", "2017",
                     "Comedies,Independent Movies,Thrillers", "R", "96 min", "United States",
                     "A hapless guy agrees to help his stunning – but psychotic – girlfriend rob her loathsome sugar daddy in a scheme as ill-conceived as it is violent."));
    s.append(NfTitle("s133", "7 (Seven)", "series", "2019",
                     "TV Shows", "TV-14", "1 Season", "India",
                     "Multiple women report their husbands as missing but when it appears they are looking for the same man, a police officer traces their cryptic connection."));
    s.append(NfTitle("s134", "7 años", "movie", "2016",
                     "Dramas,International Movies", "TV-MA", "77 min", "Spain",
                     "Loyalties are tested and cruelties revealed when four business partners spend a tense evening debating who will pay for the crime they committed."));
    s.append(NfTitle("s135", "7 Days Out", "series", "2018",
                     "Docuseries", "TV-PG", "1 Season", "United States",
                     "Witness the excitement and drama behind the scenes in the seven days leading up to major live events in the worlds of sports, fashion, space and food."));
    s.append(NfTitle("s136", "7 Din Mohabbat In", "movie", "2018",
                     "Comedies,International Movies,Music & Musicals", "TV-PG", "137 min", "Pakistan",
                     "Given just seven days by a genie to find a girl who will fall for him, a nerdy young man embarks on a madcap and messy search for his would-be wife."));
    s.append(NfTitle("s137", "7 Khoon Maaf", "movie", "2011",
                     "Dramas,International Movies,Thrillers", "TV-MA", "148 min", "India",
                     "Spiced liberally with black comedy, this Bollywood drama follows the lethal love life of a woman who marries numerous men – only to find them flawed."));
    s.append(NfTitle("s138", "72 Cutest Animals", "series", "2016",
                     "Docuseries,International TV Shows,Science & Nature TV", "TV-PG", "1 Season", "Australia",
                     "This series examines the nature of cuteness and how adorability helps some animal species to survive and thrive in a variety of environments."));
    s.append(NfTitle("s139", "72 Dangerous Animals: Asia", "series", "2018",
                     "Docuseries,International TV Shows,Science & Nature TV", "TV-14", "1 Season", "Australia",
                     "From fangs to claws to venomous stings, they all wield deadly weapons. But which creature will be crowned the fiercest of all?"));
    s.append(NfTitle("s140", "72 Dangerous Animals: Latin America", "series", "2017",
                     "Docuseries,International TV Shows,Science & Nature TV", "TV-14", "1 Season", "Australia, United States",
                     "Powerful cats, indestructible arachnids and flesh-melting pit vipers are just the beginning in this series about Latin America's deadliest creatures."));
    s.append(NfTitle("s141", "72 Dangerous Places to Live", "series", "2016",
                     "Docuseries,International TV Shows,Science & Nature TV", "TV-PG", "1 Season", "Australia",
                     "Get up close and personal with avalanches, fiery volcanoes and other natural cataclysms, and learn why some choose to live in their destructive paths."));
    s.append(NfTitle("s142", "7SEEDS", "series", "2020",
                     "Anime Series,International TV Shows", "TV-MA", "2 Seasons", "",
                     "Shy Natsu awakens as part of a group chosen to ensure the survival of humanity. Together, they have to survive on a changed Earth."));
    s.append(NfTitle("s143", "9 Months That Made You", "series", "2016",
                     "British TV Shows,Docuseries,Science & Nature TV", "TV-PG", "1 Season", "United States",
                     "Witness the wonders of human gestation through cutting-edge CGI, and learn how those nine months inside the womb can affect all aspects of one's life."));
    s.append(NfTitle("s144", "90 ML", "movie", "2019",
                     "Comedies,International Movies,Music & Musicals", "TV-MA", "123 min", "India",
                     "Four friends shake up their lives when they meet a new woman in their apartment complex who encourages them to embrace their wild sides."));
    s.append(NfTitle("s145", "93 Days", "movie", "2016",
                     "Dramas,International Movies,Thrillers", "TV-PG", "121 min", "Nigeria",
                     "Heroic health workers fight to contain an Ebola outbreak when a patient arrives in Lagos with symptoms of the deadly virus. Based on a true story."));
    s.append(NfTitle("s146", "A 2nd Chance", "movie", "2011",
                     "Children & Family Movies,Dramas,Sports Movies", "PG", "95 min", "Australia",
                     "A gymnast lacks the confidence she needs to reach the top. But with the help of her new coach, she has the chance to win a spot on the national team."));
    s.append(NfTitle("s147", "A 3 Minute Hug", "movie", "2019",
                     "Documentaries,International Movies", "TV-PG", "28 min", "Mexico, United States",
                     "This documentary captures the joy and heartbreak of families separated by the U.S.-Mexico border sharing a short but bittersweet reunion in 2018."));
    s.append(NfTitle("s148", "A Babysitter's Guide to Monster Hunting", "movie", "2020",
                     "Children & Family Movies", "TV-PG", "99 min", "United States",
                     "Recruited by a secret society of babysitters, a high schooler battles the Boogeyman and his monsters when they nab the boy she's watching on Halloween."));
    s.append(NfTitle("s149", "A Bad Moms Christmas", "movie", "2017",
                     "Comedies", "R", "104 min", "United States, China",
                     "Stressed-out moms Amy, Carla and Kiki are back, and the looming Christmas holiday gets even more daunting when their mothers show up for a visit."));
    s.append(NfTitle("s150", "A Beautiful Life", "movie", "2011",
                     "Dramas,International Movies,Romantic Movies", "TV-14", "122 min", "China, Hong Kong",
                     "After meeting under awkward circumstances, a glamorous businesswoman and a tradition-bound policeman begin a relationship that changes their lives."));
    s.append(NfTitle("s151", "A Billion Colour Story", "movie", "2016",
                     "Dramas,Independent Movies,International Movies", "TV-14", "104 min", "India",
                     "The curious child of idealistic interfaith parents observes an increasingly intolerant world as his family faces financial strain."));
    s.append(NfTitle("s152", "A Boy Called Po", "movie", "2016",
                     "Dramas,Sci-Fi & Fantasy", "PG", "94 min", "United States",
                     "After his wife's death, an overworked engineer struggles to care for his son with autism, who regresses into a fantasy world to escape real-life bullying."));
    s.append(NfTitle("s153", "A Boy Name Flora A", "series", "2017",
                     "International TV Shows,TV Comedies,TV Dramas", "TV-MA", "1 Season", "Taiwan",
                     "A 28-year-old layabout begins to re-examine his life when his dysfunctional family assembles to pay respects to a dying grandmother who won’t pass on."));
    s.append(NfTitle("s154", "A Bridge Too Far", "movie", "1977",
                     "Action & Adventure,Classic Movies,Dramas", "PG", "176 min", "United States, United Kingdom",
                     "This wartime drama details a pivotal day in 1944 when an Allied task force tried to win World War II by seizing control of key bridges in Holland."));
    s.append(NfTitle("s155", "A California Christmas", "movie", "2020",
                     "Comedies,Romantic Movies", "PG-13", "107 min", "United States",
                     "With his carefree lifestyle on the line, a wealthy charmer poses as a ranch hand to get a hardworking farmer to sell her family’s land before Christmas."));
    s.append(NfTitle("s156", "A Champion Heart", "movie", "2018",
                     "Children & Family Movies,Dramas", "G", "90 min", "United States",
                     "When a grieving teen must work off her debt to a ranch, she cares for a wounded horse that teaches her more about healing than she expected."));
    s.append(NfTitle("s157", "A Chaster Marriage", "movie", "2016",
                     "Comedies,International Movies,Romantic Movies", "TV-14", "94 min", "Turkey",
                     "Forced to wed his childhood friend, a man obsessed with football attempts to get rid of his wife in order to keep the woman he truly loves."));
    s.append(NfTitle("s158", "A Choo", "movie", "2020",
                     "Action & Adventure,International Movies,Romantic Movies", "TV-14", "101 min", "Taiwan",
                     "Determined to win the heart of his childhood crush from orphanage, EJ becomes a boxer with superpowers and soon confronts a formidable villain."));
    s.append(NfTitle("s159", "A Christmas Catch", "movie", "2018",
                     "Dramas,Romantic Movies", "TV-G", "87 min", "Canada",
                     "A cop working undercover to trail a possible diamond thief gets caught in a tricky spot when she finds new clues — and new feelings — for the suspect."));
    s.append(NfTitle("s160", "A Christmas Prince", "movie", "2017",
                     "Children & Family Movies,Dramas,Romantic Movies", "TV-PG", "92 min", "United States",
                     "Christmas comes early for an aspiring young journalist when she's sent abroad to get the scoop on a dashing prince who's poised to be king."));
    s.append(NfTitle("s161", "A Christmas Prince: The Royal Baby", "movie", "2019",
                     "Children & Family Movies,Dramas,Romantic Movies", "TV-PG", "85 min", "United States",
                     "Christmas brings the ultimate gift to Aldovia: a royal baby. But first, Queen Amber must save her family and kingdom by unwrapping a monarchy mystery."));
    s.append(NfTitle("s162", "A Christmas Prince: The Royal Wedding", "movie", "2018",
                     "Children & Family Movies,Dramas,Romantic Movies", "TV-PG", "93 min", "United States",
                     "A year after helping Richard secure the crown, Amber returns to Aldovia to plan their wedding. But her simple tastes clash with royal protocol."));
    s.append(NfTitle("s163", "A Christmas Special: Miraculous: Tales of Ladybug & Cat Noir", "movie", "2016",
                     "Movies", "TV-Y7", "22 min", "France, South Korea, Japan",
                     "Parisian teen Marinette transforms herself into superhero Ladybug to find her lonely secret crush Adrien when he runs away from home at Christmas."));
    s.append(NfTitle("s164", "A Cinderella Story", "movie", "2004",
                     "Children & Family Movies,Comedies", "PG", "95 min", "United States, Canada",
                     "Teen Sam meets the boy of her dreams at a dance before returning to toil in her stepmother's diner. Can her lost cell phone bring them together?"));
    s.append(NfTitle("s165", "A Cinderella Story: Christmas Wish", "movie", "2019",
                     "Children & Family Movies,Comedies,Music & Musicals", "PG", "86 min", "United States",
                     "Despite her vain stepmother and mean stepsisters, an aspiring singer works as an elf at a Christmas tree lot and finds her own holiday miracle."));
    s.append(NfTitle("s166", "A Clockwork Orange", "movie", "1971",
                     "Classic Movies,Cult Movies,Dramas", "R", "137 min", "United Kingdom, United States",
                     "In this dark satire from director Stanley Kubrick, a young, vicious sociopath in a dystopian England undergoes an experimental rehabilitation therapy."));
    s.append(NfTitle("s167", "A Dangerous Woman", "movie", "1993",
                     "Dramas,Romantic Movies", "R", "101 min", "United States",
                     "At the center of this engrossing melodrama is a Golden Globe-nominated turn by Debra Winger, who plays a sheltered, slow-witted woman living with her widowed Aunt Frances while working at a dry cleaners."));
    s.append(NfTitle("s168", "A Fairly Odd Summer", "movie", "2014",
                     "Children & Family Movies,Comedies", "TV-Y7", "68 min", "United States",
                     "In this live-action adventure, the gang heads to Hawaii, where Timmy learns the source of all fairy magic is in dangerous hands."));
    s.append(NfTitle("s169", "A Fall from Grace", "movie", "2020",
                     "Dramas,Thrillers", "TV-MA", "121 min", "United States",
                     "When gentle, law-abiding Grace confesses to killing her new husband, her skeptical young lawyer sets out to uncover the truth. A film by Tyler Perry."));
    s.append(NfTitle("s170", "A Family Affair", "movie", "2015",
                     "Documentaries,International Movies", "TV-PG", "111 min", "Netherlands, Denmark, South Africa",
                     "The filmmaker hunts for the missing puzzle pieces of his family history during a visit with a complex and controversial figure: his grandmother."));
    s.append(NfTitle("s171", "A Family Man", "movie", "2016",
                     "Dramas", "R", "110 min", "Canada, United States",
                     "A ruthless corporate headhunter battles his rival for a promotion while dealing with a family crisis that threatens to derail his career."));
    s.append(NfTitle("s172", "A Family Reunion Christmas", "movie", "2019",
                     "Movies", "TV-PG", "29 min", "United States",
                     "M'Dear and her sisters struggle to keep their singing act together before a church Christmas pageant while Grandpa teaches the kids a valuable lesson."));
    s.append(NfTitle("s173", "A Flying Jatt", "movie", "2016",
                     "Action & Adventure,International Movies,Sci-Fi & Fantasy", "TV-14", "142 min", "India",
                     "A timid man gets unexpected superpowers while trying to save his family's land and a sacred tree from a ruthless tycoon."));
    s.append(NfTitle("s174", "A Fortunate Man", "movie", "2018",
                     "Dramas,International Movies", "TV-MA", "168 min", "Denmark",
                     "A gifted engineer flees his austere roots to pursue wealth and success among Copenhagen's elite, but the pride propelling him threatens to be his ruin."));
    s.append(NfTitle("s175", "A Futile and Stupid Gesture", "movie", "2018",
                     "Comedies", "TV-MA", "102 min", "United States",
                     "In a brief life full of triumph and failure, \"National Lampoon\" co-founder Doug Kenney built a comedy empire, molding pop culture in the 1970s."));
    s.append(NfTitle("s176", "A Ghost Story", "movie", "2017",
                     "Dramas,Independent Movies", "R", "92 min", "United States",
                     "Following a fatal car crash, a man's spirit remains stuck in the home he shared with his wife as doors into the past and future begin to open."));
    s.append(NfTitle("s177", "A Glimpse Inside the Mind of Charles Swan III", "movie", "2012",
                     "Comedies,Independent Movies", "R", "86 min", "United States",
                     "When his girlfriend walks out and leaves him a wreck, a graphic designer – who seemed to have it all – sets out to discover where he went wrong."));
    s.append(NfTitle("s178", "A Go! Go! Cory Carson Christmas", "movie", "2020",
                     "Children & Family Movies", "TV-Y", "22 min", "United States",
                     "When a familiar-looking stranger crashes in without a memory, Cory helps him remember the magic of Christmas to save the holiday for everyone."));
    s.append(NfTitle("s179", "A Go! Go! Cory Carson Halloween", "movie", "2020",
                     "Children & Family Movies", "TV-Y", "22 min", "",
                     "Cory, Chrissy and Freddie are on the hunt for king-sized candy bars this Halloween! But are all the treats worth the trek to the spooky side of town?"));
    s.append(NfTitle("s180", "A Go! Go! Cory Carson Summer Camp", "movie", "2020",
                     "Children & Family Movies", "TV-Y", "21 min", "",
                     "Cory's spending the summer at Camp Friendship with his best friend, Freddie. But jealousy flares when Freddie brings his cousin Rosie along for the ride."));
    s.append(NfTitle("s181", "A Good Wife", "series", "2013",
                     "International TV Shows,Romantic TV Shows,TV Dramas", "TV-14", "1 Season", "Taiwan",
                     "As her seemingly idyllic life begins to crumble, a lonely woman in a restrictive marriage starts an affair with a gentle bookstore owner."));
    s.append(NfTitle("s182", "A Grand Night In: The Story of Aardman", "movie", "2015",
                     "Documentaries", "TV-PG", "59 min", "United Kingdom",
                     "Aardman Animations icons Wallace & Gromit, Morph and more join fans and collaborators for a retrospective celebrating the studio's 40th anniversary."));
    s.append(NfTitle("s183", "A Gray State", "movie", "2017",
                     "Documentaries", "TV-MA", "93 min", "United States",
                     "This documentary dissects the case of a filmmaker whose death, along with the deaths of his wife and daughter, sparked alt-right conspiracy theories."));
    s.append(NfTitle("s184", "A Haunted House", "movie", "2013",
                     "Comedies,Horror Movies", "R", "86 min", "United States",
                     "This spoof on scary movies follows a young couple settling into a new home, where an evil spirit — and horrifyingly hilarious antics — await."));
    s.append(NfTitle("s185", "A Haunting at Silver Falls: The Return", "movie", "2019",
                     "Horror Movies,Independent Movies", "TV-14", "80 min", "United States",
                     "When the ghost of her serial killer aunt seemingly resurfaces, Jordan must return to Silver Falls in hopes of finally putting an end to the torment."));
    s.append(NfTitle("s186", "A Heavy Heart", "movie", "2015",
                     "Dramas,Independent Movies,International Movies", "TV-MA", "109 min", "Germany",
                     "Long past his heyday, a still-virile boxer tries to reconnect with his daughter as he confronts the onset of a terminal, degenerative neural disorder."));
    s.append(NfTitle("s187", "A Holiday Engagement", "movie", "2011",
                     "Children & Family Movies,Comedies,Romantic Movies", "TV-G", "92 min", "United States",
                     "Hilary's plan to hire a good-looking guy to act as her boyfriend backfires when she brings him home for the holidays to try and fool her family."));
    s.append(NfTitle("s188", "A Home with A View", "movie", "2019",
                     "Comedies,International Movies", "TV-MA", "92 min", "Hong Kong",
                     "When a neighbor blocks their view of the city with a commercial billboard, a Hong Kong family resorts to drastic, imaginative measures to take it down."));
    s.append(NfTitle("s189", "A House of Blocks", "series", "2017",
                     "International TV Shows,TV Dramas,TV Horror", "TV-PG", "1 Season", "Taiwan",
                     "Yijuan and her mentally ill sister Kaiqi struggle to be happy in the face of misfortune, criminal intrigue, marital strife, an exorcism and a ghost."));
    s.append(NfTitle("s190", "A Kid from Coney Island", "movie", "2019",
                     "Documentaries,Sports Movies", "TV-MA", "89 min", "United States, China",
                     "From gifted athlete to professional NBA hooper, Coney Island's Stephon Marbury navigates the pressures, pitfalls and peaks of his basketball journey."));
    s.append(NfTitle("s191", "A Kind of Murder", "movie", "2016",
                     "Thrillers", "R", "96 min", "United States",
                     "Obsessed with an unsolved murder case, a crime novelist stuck in an unhappy marriage fantasizes about killing his wife, who soon turns up dead."));
    s.append(NfTitle("s192", "A Korean Odyssey", "series", "2017",
                     "International TV Shows,Korean TV Shows,Romantic TV Shows", "TV-MA", "1 Season", "South Korea",
                     "A self-serving mythical creature's bid for invincibility backfires when he finds himself at the mercy of a woman who can see otherworldly beings."));
    s.append(NfTitle("s193", "A Land Imagined", "movie", "2019",
                     "Dramas,International Movies", "TV-MA", "95 min", "France, Netherlands, Singapore",
                     "A cop in Singapore investigates the disappearance of a Chinese migrant construction worker who spent sleepless nights playing a mysterious video game."));
    s.append(NfTitle("s194", "A Leaf of Faith", "movie", "2018",
                     "Documentaries", "TV-MA", "87 min", "United States",
                     "This documentary takes a deep dive into the benefits, dangers and lingering questions around Kratom leaf as an alternative to opioid painkillers."));
    s.append(NfTitle("s195", "A Life of Speed: The Juan Manuel Fangio Story", "movie", "2020",
                     "Documentaries,International Movies,Sports Movies", "TV-14", "93 min", "Argentina",
                     "Juan Manuel Fangio was the Formula One king, winning five world championships in the early 1950s — before protective gear or safety features were used."));
    s.append(NfTitle("s196", "A Lion in the House", "series", "2006",
                     "TV Shows", "TV-14", "1 Season", "United States",
                     "Five kids and their resilient families navigate the treatments and traumas of pediatric cancer in this documentary filmed over the course of six years."));
    s.append(NfTitle("s197", "A Little Chaos", "movie", "2014",
                     "Dramas,Independent Movies,International Movies", "R", "113 min", "United Kingdom",
                     "A willful young woman is hired to design a garden at Versailles for Louis XIV. Soon, she's ensnared in political and romantic complications."));
    s.append(NfTitle("s198", "A Little Help with Carol Burnett", "series", "2018",
                     "Stand-Up Comedy & Talk Shows,TV Comedies", "TV-G", "1 Season", "United States",
                     "Comedy icon Carol Burnett returns to TV with a panel of clever kids, who help adults and celebrity guests solve their problems with brutal honesty."));
    s.append(NfTitle("s199", "A Little Thing Called First Love", "series", "2019",
                     "International TV Shows,Romantic TV Shows,Teen TV Shows", "TV-G", "1 Season", "China",
                     "A shy college student with a knack for drawing develops a crush on a musically gifted classmate and embarks on a journey of self-discovery."));
    s.append(NfTitle("s200", "A Love So Beautiful", "series", "2020",
                     "International TV Shows,Romantic TV Shows,TV Comedies", "TV-PG", "1 Season", "South Korea",
                     "Love is as tough as it is sweet for a lovestruck teenager, whose relationship with her next-door neighbor transforms as they grow into adulthood."));
    s.append(NfTitle("s201", "A Love Song for Latasha", "movie", "2020",
                     "Documentaries", "TV-PG", "20 min", "United States",
                     "The killing of Latasha Harlins became a flashpoint for the 1992 LA uprising. This documentary evocatively explores the 15-year-old's life and dreams."));
    s.append(NfTitle("s202", "A Love Story", "movie", "2007",
                     "Dramas,International Movies,Romantic Movies", "TV-14", "117 min", "Philippines",
                     "Self-made millionaire Ian thinks he's found happiness when he marries caring Joanna, but his love is put to the test when he meets stewardess Karyn."));
    s.append(NfTitle("s203", "A Man Called God", "series", "2010",
                     "Crime TV Shows,International TV Shows,Korean TV Shows", "TV-14", "1 Season", "South Korea",
                     "Raised in America, government agent Choi Kang-Ta returns to Korea with the skills of an assassin and a thirst for revenge on his father’s killers."));
    s.append(NfTitle("s204", "A Mighty Team", "movie", "2016",
                     "Comedies,International Movies,Sports Movies", "TV-MA", "97 min", "France",
                     "When a fit of anger leads to a serious injury, a sidelined soccer star returns to his hometown and reluctantly agrees to train the local youth."));
    s.append(NfTitle("s205", "A Mission in an Old Movie", "movie", "2012",
                     "Comedies,International Movies", "TV-MA", "98 min", "",
                     "A young man struggles with his overbearing mother while looking for romance and a way to kick-start his show business career."));
    s.append(NfTitle("s206", "A Monster Calls", "movie", "2016",
                     "Dramas,Sci-Fi & Fantasy", "PG-13", "108 min", "United Kingdom, Spain, United States",
                     "Overwhelmed by his mother’s illness, a young boy begins to understand human complexity through the fantastic tales of a consoling tree monster."));
    s.append(NfTitle("s207", "A Most Violent Year", "movie", "2014",
                     "Dramas,Thrillers", "R", "125 min", "United Arab Emirates, United States",
                     "Abel Morales tries to avoid corruption's easy path as he pursues the American Dream amid an increasingly violent business war in 1981 New York City."));
    s.append(NfTitle("s208", "A Murder in the Park", "movie", "2014",
                     "Documentaries", "PG-13", "91 min", "United States",
                     "This documentary excoriates a noted anti-death-penalty activist and his team, whose questionable methods got a convicted killer freed in 1999."));
    s.append(NfTitle("s209", "A new Capitalism", "movie", "2017",
                     "Documentaries,International Movies", "TV-14", "76 min", "Brazil",
                     "Entrepreneurs worldwide explore alternatives to current capitalist structures, advocating for profitable businesses that also tackle social inequality."));
    s.append(NfTitle("s210", "A New York Christmas Wedding", "movie", "2020",
                     "Dramas,LGBTQ Movies,Romantic Movies", "TV-MA", "90 min", "United States",
                     "As her wedding nears, a bride-to-be is visited by an angel who reveals what could have been if she'd followed feelings for her childhood best friend."));
    s.append(NfTitle("s211", "A Night at the Roxbury", "movie", "1998",
                     "Comedies,Cult Movies", "PG-13", "82 min", "United States",
                     "After a run-in with Richard Grieco, dimwits Doug and Steve gain entry to a swanky nightclub in this comedy based on a \"Saturday Night Live\" sketch."));
    s.append(NfTitle("s212", "A Noble Intention", "movie", "2015",
                     "Dramas,International Movies", "TV-MA", "111 min", "Netherlands",
                     "In 1888 Amsterdam, a headstrong violin maker finds himself immersed in peril and tragedy after challenging businessmen who threaten his community."));
    s.append(NfTitle("s213", "A Patch of Fog", "movie", "2015",
                     "Dramas,Thrillers", "TV-MA", "92 min", "United Kingdom",
                     "When a guard catches a writer-television host shoplifting, instead of turning him in, he only asks to be a friend, then begins to rule his life."));
    s.append(NfTitle("s214", "A Perfect Crime", "series", "2020",
                     "Crime TV Shows,Docuseries,International TV Shows", "TV-14", "1 Season", "Germany",
                     "This docuseries investigates the 1991 killing of politician Detlev Rohwedder, an unsolved mystery at the heart of Germany's tumultuous reunification."));
    s.append(NfTitle("s215", "A Perfect Ending", "movie", "2012",
                     "Dramas,Independent Movies,LGBTQ Movies", "TV-MA", "110 min", "United States",
                     "After confessing an unusual secret, a repressed wife – prompted by her friends – decides to explore her sexuality with a high-priced call girl."));
    s.append(NfTitle("s216", "A Perfect Man", "movie", "2013",
                     "Dramas,Independent Movies,Romantic Movies", "R", "94 min", "United States",
                     "Nina thinks her husband, James, is cheating on her, and she sets out to prove it by calling him and pretending to be another woman."));
    s.append(NfTitle("s217", "A Plastic Ocean", "movie", "2016",
                     "Documentaries", "TV-14", "100 min", "United Kingdom, Hong Kong",
                     "When he discovers the world's oceans brimming with plastic waste, a documentary filmmaker investigates the pollution's environmental impacts."));
    s.append(NfTitle("s218", "A Princess for Christmas", "movie", "2011",
                     "Children & Family Movies,Dramas,Romantic Movies", "TV-PG", "91 min", "United States",
                     "At the invitation of a relative, young Jules Daly travels with her niece and nephew to a castle in Europe, where Jules falls for a dashing prince."));
    s.append(NfTitle("s219", "A Queen Is Born", "series", "2020",
                     "International TV Shows,Reality TV", "TV-14", "1 Season", "Brazil",
                     "Gloria Groove and Alexia Twister make drag dreams come true as they help six artists find the confidence to own the stage in this makeover show."));
    s.append(NfTitle("s220", "A Remarkable Tale", "movie", "2019",
                     "Comedies,International Movies", "TV-MA", "94 min", "Spain",
                     "The residents of an isolated town look to revive their home's identity when a group of foreigners unexpectedly arrive, bringing culture shock with them."));
    s.append(NfTitle("s221", "A Russell Peters Christmas", "movie", "2011",
                     "Stand-Up Comedy", "NR", "44 min", "Canada",
                     "Inspired by the variety shows of the 1970s, \"A Russell Peters Christmas\" is a sweet, silly, sentimental and, most of all, funny Christmas special."));
    s.append(NfTitle("s222", "A Scandall", "movie", "2016",
                     "International Movies,Thrillers", "TV-MA", "108 min", "India",
                     "A film school graduate is interested in making a movie about his girlfriend's uncle, who claims that he can see his long-dead daughter."));
    s.append(NfTitle("s223", "A Second Chance", "movie", "2015",
                     "Dramas,International Movies,Romantic Movies", "TV-MA", "130 min", "Philippines",
                     "Following their storybook wedding, Popoy and Basha find married life – and starting a business together – more challenging than they ever imagined."));
    s.append(NfTitle("s224", "A Secret Love", "movie", "2020",
                     "Documentaries,LGBTQ Movies", "TV-14", "83 min", "United States",
                     "Amid shifting times, two women kept their decades-long love a secret. But coming out later in life comes with its own set of challenges."));
    s.append(NfTitle("s225", "A Separation", "movie", "2011",
                     "Dramas,International Movies", "PG-13", "123 min", "Iran, France",
                     "Amid an impasse in his marriage, a father in Tehran is beset by a bitter feud involving the family of a pious caretaker he hired for his aging dad."));
    s.append(NfTitle("s226", "A Series of Unfortunate Events", "series", "2019",
                     "Kids' TV,TV Action & Adventure,TV Comedies", "TV-PG", "3 Seasons", "United States",
                     "The extraordinary Baudelaire orphans face trials, tribulations and the evil Count Olaf in their fateful quest to unlock long-held family secrets."));
    s.append(NfTitle("s227", "A Serious Man", "movie", "2009",
                     "Comedies,Independent Movies", "R", "106 min", "United States, United Kingdom, France",
                     "With every aspect of his life unraveling, a Jewish physics professor seeks out three rabbis for spiritual guidance."));
    s.append(NfTitle("s228", "A Shaun the Sheep Movie: Farmageddon", "movie", "2020",
                     "Children & Family Movies,Comedies", "G", "87 min", "United Kingdom, France, Belgium, United States, China",
                     "Shaun and the flock race to help an adorable alien find her way home after her ship crash-lands near Mossy Bottom Farm and sparks a UFO frenzy."));
    s.append(NfTitle("s229", "A Silent Voice", "movie", "2016",
                     "Anime Features,International Movies", "TV-14", "130 min", "Japan",
                     "A former class bully reaches out to the deaf girl he'd tormented in grade school. He feels unworthy of redemption but tries to make things right."));
    s.append(NfTitle("s230", "A Single Man", "movie", "2009",
                     "Dramas,Independent Movies,LGBTQ Movies", "R", "100 min", "United States",
                     "This stream-of-consciousness drama centers on a day in the life of a gay college professor who's reeling from his longtime lover's recent death."));
    s.append(NfTitle("s231", "A Sort of Family", "movie", "2017",
                     "Dramas,International Movies,Thrillers", "TV-14", "96 min", "Argentina, Brazil, France, Poland, Germany, Denmark",
                     "An Argentine doctor faces legal and ethical challenges when she travels to the countryside to pick up the infant she has been waiting to adopt."));
    s.append(NfTitle("s232", "A Sort of Homecoming", "movie", "2015",
                     "Dramas,Independent Movies", "TV-PG", "89 min", "United States",
                     "Ace news producer Amy is called back to her Louisiana hometown where memories of what used to be – and what might have been – come flooding back."));
    s.append(NfTitle("s233", "A Stoning in Fulham County", "movie", "1988",
                     "Dramas", "TV-14", "95 min", "United States",
                     "After reckless teens kill an Amish child, a prosecutor attempts to bring the youths to justice despite the condemnation he faces from the community."));
    s.append(NfTitle("s234", "A StoryBots Christmas", "movie", "2017",
                     "Children & Family Movies,Comedies", "TV-Y", "26 min", "United States",
                     "Bo thinks her holiday gift-giving isn't good enough, so she heads north to study under Santa Claus – only to find he's missing."));
    s.append(NfTitle("s235", "A Sun", "movie", "2019",
                     "Dramas,Independent Movies,International Movies", "TV-MA", "156 min", "Taiwan",
                     "A family reckons with the aftermath of their younger son's incarceration and a greater misfortune that follows."));
    s.append(NfTitle("s236", "A Taiwanese Tale of Two Cities", "series", "2018",
                     "International TV Shows,TV Dramas", "TV-MA", "1 Season", "Taiwan",
                     "A Taipei doctor and a San Francisco engineer swap homes in a daring pact, embarking on journeys filled with trials, secrets and unexpected encounters."));
    s.append(NfTitle("s237", "A Tale of Love and Darkness", "movie", "2015",
                     "Dramas,International Movies", "PG-13", "98 min", "Israel, United States",
                     "Based on the memoirs of author Amos Oz, this poetic drama shares his family's tale of suffering and survival in the early years of independent Israel."));
    s.append(NfTitle("s238", "A Tale of Two Kitchens", "movie", "2019",
                     "Documentaries,International Movies", "TV-PG", "30 min", "United States, Mexico",
                     "Mexico City restaurant star Gabriela Cámara opens sister eatery Cala in San Francisco, with a similar menu and unusually welcoming kitchen culture."));
    s.append(NfTitle("s239", "A Thin Line Between Love & Hate", "movie", "1996",
                     "Comedies,Romantic Movies", "R", "108 min", "United States",
                     "When a philandering club promoter sets out to woo a rich, glamorous woman, he has no clue just how much mayhem he's about to unleash on his life."));
    s.append(NfTitle("s240", "A Thousand Goodnights", "series", "2019",
                     "International TV Shows,TV Dramas", "TV-14", "1 Season", "Taiwan",
                     "To carry out her dad's wish and discover her roots, Dai Tian-qing embarks on a journey around Taiwan and finds love and redemption on the way."));
    s.append(NfTitle("s241", "A Thousand Words", "movie", "2012",
                     "Comedies", "PG-13", "92 min", "United States",
                     "When he learns his karma will permit him to speak just a thousand more words before he dies, fast-talking agent Jack must make every syllable count."));
    s.append(NfTitle("s242", "A Touch of Green", "series", "2016",
                     "International TV Shows,TV Dramas", "TV-MA", "1 Season", "Taiwan",
                     "Amid the turmoil of China's civil war, families of Kuomintang pilots face the pain of leaving their homes but find strength in their shared bonds."));
    s.append(NfTitle("s243", "A Trash Truck Christmas", "movie", "2020",
                     "Children & Family Movies", "TV-Y", "28 min", "",
                     "When Santa crash-lands in the junkyard on Christmas Eve, Hank, Trash Truck and their animal friends all have a hand in rescuing the holiday for everyone."));
    s.append(NfTitle("s244", "A Trip to Jamaica", "movie", "2016",
                     "Comedies,International Movies", "TV-MA", "99 min", "Nigeria",
                     "A newly engaged couple's romantic vacation in Jamaica turns into a mischievous adventure that tests their union in wild and unexpected ways."));
    s.append(NfTitle("s245", "A truthful Mother", "movie", "2020",
                     "Children & Family Movies", "TV-Y7", "85 min", "India",
                     "Facing a drought, a hungry tiger and a noble cow have an extraordinary encounter in this fable based on a children’s book and a Kannada folk song."));
    s.append(NfTitle("s246", "A Twelve Year Night", "movie", "2018",
                     "Dramas,International Movies", "TV-MA", "122 min", "Uruguay, Argentina, Spain",
                     "Future Uruguayan president José Mujica and his fellow Tupamaro political prisoners fight to survive 12 years of solitary confinement and torture."));
    s.append(NfTitle("s247", "A Very Country Christmas", "movie", "2017",
                     "Music & Musicals,Romantic Movies", "TV-G", "84 min", "United States, Canada",
                     "When an unfulfilled country music star hides out in his hometown, he meets a budding interior designer who finds her way into his heart."));
    s.append(NfTitle("s248", "A Very Murray Christmas", "movie", "2015",
                     "Comedies,Music & Musicals", "TV-14", "57 min", "United States",
                     "Bill Murray rounds up an all-star cast for an evening of music, mischief and barroom camaraderie in this irreverent twist on holiday variety shows."));
    s.append(NfTitle("s249", "A Very Secret Service", "series", "2018",
                     "International TV Shows,TV Comedies", "TV-MA", "2 Seasons", "France",
                     "At the height of the Cold War in 1960, André Merlaux joins the French Secret Service and contends with enemies both foreign and bureaucratic."));
    s.append(NfTitle("s250", "A Very Special Love", "movie", "2008",
                     "International Movies,Romantic Movies", "TV-G", "107 min", "Philippines",
                     "After landing a job working for her longtime crush, an optimistic woman realizes that the man of her dreams isn't exactly who she envisioned."));
    s.append(NfTitle("s251", "A Walk to Remember", "movie", "2002",
                     "Dramas,Romantic Movies", "PG", "102 min", "United States",
                     "When bad boy Landon is cast opposite campus bookworm Jamie in a high school play, romance blooms — until tragedy threatens to tear them apart."));
    s.append(NfTitle("s252", "A Wednesday", "movie", "2008",
                     "Action & Adventure,Dramas,International Movies", "TV-MA", "103 min", "India",
                     "After receiving an anonymous tip about a bomb, a police commissioner must negotiate with the terrorist, who demands the release of four militants."));
    s.append(NfTitle("s253", "A Week in Watts", "movie", "2017",
                     "Documentaries", "TV-14", "91 min", "United States",
                     "Los Angeles police officers embark on an innovative program mentoring promising students from the harsh, gang-infested Watts neighborhood."));
    s.append(NfTitle("s254", "A Whisker Away", "movie", "2020",
                     "Anime Features,Children & Family Movies,International Movies", "TV-PG", "104 min", "Japan",
                     "A peculiar girl transforms into a cat to catch her crush's attention. But before she realizes it, the line between human and animal starts to blur."));
    s.append(NfTitle("s255", "A Witches' Ball", "movie", "2017",
                     "Children & Family Movies", "PG", "91 min", "Canada",
                     "Beatrix can't wait to be inducted as a witch, but an unfortunate incident threatens to take her pending title away if she doesn't act fast."));
    s.append(NfTitle("s256", "A Wrinkle in Time", "movie", "2018",
                     "Children & Family Movies", "PG", "110 min", "United States",
                     "Years after their father disappears, Meg and her younger brother Charles Wallace cross galaxies on a quest to save him from the heart of darkness."));
    s.append(NfTitle("s257", "A Year In Space", "series", "2015",
                     "Docuseries,International TV Shows,Science & Nature TV", "TV-PG", "1 Season", "United States",
                     "Two astronauts attempt to brave a life in Earth's orbit on a record-setting mission to see if humans have the endurance to survive a flight to Mars."));
    s.append(NfTitle("s258", "A Yellow Bird", "movie", "2016",
                     "Dramas,Independent Movies,International Movies", "TV-MA", "111 min", "Singapore, France",
                     "In Singapore, a homeless ex-convict hoping to reunite with his family forms a bond with a Chinese sex worker while serving as her bodyguard."));
    s.append(NfTitle("s259", "A Young Doctor's Notebook and Other Stories", "series", "2013",
                     "British TV Shows,TV Comedies,TV Dramas", "TV-MA", "2 Seasons", "United Kingdom",
                     "Set during the Russian Revolution, this comic miniseries is based on a doctor's memories of his early career working in an out-of-the-way village."));
    s.append(NfTitle("s260", "A.D. Kingdom and Empire", "series", "2015",
                     "TV Dramas", "TV-14", "1 Season", "United States",
                     "In the wake of Jesus Christ's crucifixion, his apostles dedicate themselves to spreading his message, risking their freedom and their lives."));
    s.append(NfTitle("s261", "A.I.C.O.", "series", "2018",
                     "Anime Series,International TV Shows", "TV-14", "1 Season", "Japan",
                     "Everything Aiko knew was a lie. Now she's joining a team of Divers to reach the place where the Burst began to stop it for good and save her family."));
    s.append(NfTitle("s262", "A.M.I.", "movie", "2019",
                     "Horror Movies", "TV-MA", "77 min", "Canada",
                     "After losing her mother, a teenage girl bonds with her phone’s artificial intelligence app, a relationship that soon takes a dark and violent turn."));
    s.append(NfTitle("s263", "A.X.L.", "movie", "2018",
                     "Action & Adventure,Children & Family Movies,Independent Movies", "PG", "99 min", "United States",
                     "Young motocross racer Miles Hill helps a top-secret robotic combat dog evade its ruthless creator and the military, who are in hot pursuit."));
    s.append(NfTitle("s264", "Aşk Tesadüfleri Sever", "movie", "2011",
                     "Dramas,International Movies,Romantic Movies", "TV-MA", "122 min", "Turkey",
                     "A series of coincidences brings two star-crossed lovers together, but fate pulls them apart until they encounter each other again in Istanbul."));
    s.append(NfTitle("s265", "Aadu 2", "movie", "2017",
                     "Comedies,International Movies", "TV-MA", "150 min", "India",
                     "When Shaji Pappan and his gang of goofy outlaws find themselves low on funds, they plan to once again win a hefty prize in a tug-of-war competition."));
    s.append(NfTitle("s266", "Aagey Se Right", "movie", "2009",
                     "Comedies,International Movies,Music & Musicals", "TV-14", "117 min", "India",
                     "A cop full of self-doubt loses his gun five days before he's due to leave the force, and becomes an unexpected hero as he tries to retrieve it."));
    s.append(NfTitle("s267", "Aajcha Divas Majha", "movie", "2013",
                     "Comedies,Dramas,International Movies", "TV-PG", "126 min", "India",
                     "A conscientious politician helps an elderly singer overcome the obstacles placed in his path a by a powerful bureaucracy."));
    s.append(NfTitle("s268", "Aakhri Adaalat", "movie", "1988",
                     "Action & Adventure,International Movies", "TV-14", "148 min", "India",
                     "An intrepid police inspector forced into a desk job must take matters into his own hands when ruthless criminals are released on a technicality."));
    s.append(NfTitle("s269", "Aalorukkam", "movie", "2018",
                     "Dramas,Independent Movies,International Movies", "TV-PG", "122 min", "India",
                     "When an aging father seeks the help of a doctor and a journalist to look for the son who left him 16 years ago, he learns some surprising truths."));
    s.append(NfTitle("s270", "Aamir", "movie", "2008",
                     "International Movies,Thrillers", "TV-14", "95 min", "India",
                     "In this high-tension thriller, an anonymous caller on a cell phone threatens to harm a man's family if he doesn't carry out every order."));
    s.append(NfTitle("s271", "Aapla Manus", "movie", "2018",
                     "Dramas,International Movies,Thrillers", "TV-14", "133 min", "India",
                     "When a man falls from his balcony, an investigator questions the victim’s family, determined to uncover a darker truth behind the alleged accident."));
    s.append(NfTitle("s272", "Aarakshan", "movie", "2011",
                     "Dramas,International Movies", "TV-14", "164 min", "India",
                     "The decision by India's supreme court to establish caste-based reservations for jobs in education causes conflict between a teacher and his mentor."));
    s.append(NfTitle("s273", "Aashayein", "movie", "2010",
                     "Dramas,International Movies", "TV-14", "115 min", "India",
                     "When he learns he has terminal cancer, a cynical gambler and chronic smoker ditches his fiancée, snubs doctors and checks into a nursing home to die."));
    s.append(NfTitle("s274", "Aashik Awara", "movie", "1993",
                     "Dramas,International Movies,Music & Musicals", "TV-14", "154 min", "India",
                     "Raised by a kindly thief, orphaned Jimmy goes on the run from goons and falls in love with Jyoti, whose father indirectly caused his parents' deaths."));
    s.append(NfTitle("s275", "Aata Pita", "movie", "2010",
                     "Comedies,Dramas,International Movies", "TV-14", "133 min", "India",
                     "A municipal clerk with literary ambitions stalks a loan recovery officer on whom he has decided to base his story's central character."));
    s.append(NfTitle("s276", "Aaviri", "movie", "2019",
                     "Horror Movies,International Movies,Thrillers", "TV-14", "110 min", "India",
                     "After losing their first child in an accident, a couple moves to a palatial home, where their young daughter comes under the spell of an eerie spirit."));
    s.append(NfTitle("s277", "Abby Hatcher", "series", "2019",
                     "Kids' TV", "TV-Y", "1 Season", "United States, Canada",
                     "A big-hearted girl helps her Fuzzly friends who live in her family's hotel with exploring feelings, fixing mishaps and embracing their special quirks."));
    s.append(NfTitle("s278", "Abby Sen", "movie", "2015",
                     "Comedies,Dramas,International Movies", "TV-PG", "127 min", "India",
                     "After losing his seventh job in a row, a TV producer and sci-fi buff travels back in time to 1980, where his employment prospects improve."));
    s.append(NfTitle("s279", "ABCD 2", "movie", "2015",
                     "Dramas,International Movies,Music & Musicals", "TV-14", "146 min", "India",
                     "After being accused of cheating on a reality TV show, a dance troupe seeks to redeem itself by winning the World Hip-Hop Dance Championship."));
    s.append(NfTitle("s280", "ABCD: Any Body Can Dance", "movie", "2013",
                     "Dramas,International Movies", "TV-14", "136 min", "India",
                     "Thrown out of the elite dance studio he founded, Vishnu's spirits rebound when he sees young street dancers prepping for an annual contest."));
    s.append(NfTitle("s281", "Abdo Mota", "movie", "2012",
                     "Action & Adventure,Dramas,International Movies", "TV-MA", "100 min", "Egypt",
                     "Following the mysterious death of his parents, a young man finds himself enmeshed in the dark world of drugs and crime."));
    s.append(NfTitle("s282", "Abducted in Plain Sight", "movie", "2017",
                     "Documentaries", "TV-14", "90 min", "United States",
                     "In this true crime documentary, a family falls prey to the manipulative charms of a neighbor, who abducts their adolescent daughter. Twice."));
    s.append(NfTitle("s283", "Abdullah, The Final Witness", "movie", "2016",
                     "Dramas,International Movies", "TV-14", "88 min", "Pakistan",
                     "Inspired by real events, this drama follows a trucker jailed after driving five ill-fated travelers across Quetta, and the officer taking on his case."));
    s.append(NfTitle("s284", "Abhinetri", "movie", "2016",
                     "Comedies,International Movies,Sci-Fi & Fantasy", "TV-14", "131 min", "India",
                     "Due to family pressure, a corporate man reluctantly marries a woman from the village, but in their new home, she abruptly assumes a different persona."));
    s.append(NfTitle("s285", "Abnormal Summit", "series", "2017",
                     "International TV Shows,Korean TV Shows,Stand-Up Comedy & Talk Shows", "TV-PG", "2 Seasons", "South Korea",
                     "Led by a trio of Korean celebs, a multinational panel of men engage in – usually – lighthearted debates on issues that surround Korea and beyond."));
    s.append(NfTitle("s286", "Abominable Christmas", "movie", "2012",
                     "Children & Family Movies,Comedies", "TV-Y7", "44 min", "United States",
                     "Two small abominable snowmen flee their mountain to escape a scientist who's trying to capture them and end up spending Christmas with a human family."));
    s.append(NfTitle("s287", "About a Boy", "movie", "2002",
                     "Comedies,Dramas,International Movies", "PG-13", "101 min", "United Kingdom, United States, France, Germany",
                     "Hip, irresponsible Londoner Will invents an imaginary son and starts attending single-parent meetings to find available women."));
    s.append(NfTitle("s288", "About Time", "movie", "2013",
                     "Comedies,Dramas,International Movies", "R", "123 min", "United Kingdom",
                     "When Tim learns that the men in his family can travel in time and change their own lives, he decides to go back and win the woman of his dreams."));
    s.append(NfTitle("s289", "Abstract: The Art of Design", "series", "2019",
                     "Docuseries", "TV-14", "2 Seasons", "United States",
                     "Step inside the minds of the most innovative designers in a variety of disciplines and learn how design impacts every aspect of life."));
    s.append(NfTitle("s290", "Absurd Planet", "series", "2020",
                     "Docuseries,Science & Nature TV,TV Comedies", "TV-PG", "1 Season", "United States",
                     "A cast of quirky critters and Mother Nature herself narrate this funny science series, which peeks into the lives of Earth’s most incredible animals."));
    s.append(NfTitle("s291", "Abyss", "series", "2019",
                     "Crime TV Shows,International TV Shows,Korean TV Shows", "TV-14", "1 Season", "South Korea",
                     "After meeting an untimely demise in separate incidents, Cha Min and Go Se-yeon discover they’ve come back to life in new bodies they don’t recognize."));
    s.append(NfTitle("s292", "Abzurdah", "movie", "2015",
                     "Dramas,International Movies", "TV-MA", "90 min", "Argentina",
                     "A disenfranchised 16-year-old girl connects to an older man on the internet and after a brief one-sided affair descends into obsession and anorexia."));
    s.append(NfTitle("s293", "Acapulco La vida va", "movie", "2016",
                     "Comedies,Dramas,International Movies", "TV-MA", "89 min", "Mexico",
                     "Three lifelong friends travel to Acapulco ostensibly so one of them can find his lost love, but the real reason behind the trip holds some surprises."));
    s.append(NfTitle("s294", "Accident", "movie", "2009",
                     "International Movies,Thrillers", "TV-MA", "87 min", "Hong Kong",
                     "A contract killer skilled at staging lethal accidents fears he may be a victim of his own strategy when an accomplice dies in a mysterious mishap."));
    s.append(NfTitle("s295", "Accidentally in Love", "series", "2018",
                     "International TV Shows,Romantic TV Shows,TV Comedies", "TV-14", "1 Season", "China",
                     "Rejecting the demands of her wealthy family, a young woman poses as an ordinary college student and crosses paths with a stoic pop star at school."));
    s.append(NfTitle("s296", "Across Grace Alley", "movie", "2013",
                     "Dramas", "TV-PG", "24 min", "United States",
                     "A young boy, upset by his parents' divorce, becomes infatuated with his grandmother's neighbor, an entrancing dancer he watches through a window."));
    s.append(NfTitle("s297", "Across The Line", "movie", "2015",
                     "Dramas,Sports Movies", "TV-MA", "88 min", "Canada",
                     "A black high school student sets his sights for the National Hockey League, but rising racial tensions in his community may jeopardize his goals."));
    s.append(NfTitle("s298", "Across the Universe", "movie", "2007",
                     "Dramas,Music & Musicals,Romantic Movies", "PG-13", "133 min", "United States, United Kingdom",
                     "An American girl and a British lad fall in love amid the social and political upheaval of the 1960s, in this musical featuring songs by the Beatles."));
    s.append(NfTitle("s299", "Act of Valor", "movie", "2012",
                     "Action & Adventure", "R", "110 min", "United States",
                     "An elite squad of Navy SEALs is tasked with rescuing a kidnapped CIA agent from a lethal terrorist cell."));
    s.append(NfTitle("s300", "Act of Vengeance", "movie", "2010",
                     "Action & Adventure,International Movies", "NR", "112 min", "Turkey, United States",
                     "Two Turkish agents are sent to New York City on a mission to capture a notorious terrorist known only as \"Dejjal\" (Arabic for Antichrist)."));
    s.append(NfTitle("s301", "Action Replayy", "movie", "2010",
                     "Children & Family Movies,Comedies,International Movies", "TV-PG", "129 min", "India",
                     "Sick of his parents’ constant squabbling, a young man travels back in time to the onset of their romance, with plans to set them up for marital bliss."));
    s.append(NfTitle("s302", "Acts of Vengeance", "movie", "2017",
                     "Action & Adventure,International Movies", "R", "87 min", "Bulgaria, United States",
                     "Devastated by the murder of his wife and child, a formerly fast-talking lawyer takes a vow of silence and trains himself for a mission of revenge."));
    s.append(NfTitle("s303", "Acts of Violence", "movie", "2018",
                     "Action & Adventure", "R", "87 min", "Canada",
                     "When his future sister-in-law is kidnapped by human traffickers, a military veteran joins forces with his brothers and a world-weary cop to rescue her."));
    s.append(NfTitle("s304", "Ad Vitam", "series", "2018",
                     "Crime TV Shows,International TV Shows,TV Dramas", "TV-MA", "1 Season", "France",
                     "In a future where regeneration technology lets humans live indefinitely, a cop and a troubled young woman investigate a strange wave of youth suicides."));
    s.append(NfTitle("s305", "Adú", "movie", "2020",
                     "Dramas,International Movies", "TV-MA", "120 min", "Spain",
                     "Near a Spanish town in northern Africa, a child takes a painful journey, a father reconnects with his daughter and a coast guard is guilt stricken."));
    s.append(NfTitle("s306", "Adam Devine: Best Time of Our Lives", "movie", "2019",
                     "Stand-Up Comedy", "TV-MA", "59 min", "United States",
                     "Frenetic comic Adam Devine talks teen awkwardness, celebrity encounters, his \"Pitch Perfect\" audition and more in a special from his hometown of Omaha."));
    s.append(NfTitle("s307", "Adam Ruins Everything", "series", "2018",
                     "TV Comedies", "TV-14", "1 Season", "United States",
                     "Education can be fun – and funny. Comedian Adam Conover bursts misconceptions, deconstructs topics and leaves with positive takeaways."));
    s.append(NfTitle("s308", "ADAM SANDLER 100% FRESH", "movie", "2018",
                     "Stand-Up Comedy", "TV-MA", "74 min", "United States",
                     "From \"Heroes\" to \"Ice Cream Ladies\" – Adam Sandler's comedy special hits you with new songs and jokes in an unexpected, groundbreaking way."));
    s.append(NfTitle("s309", "Adam: His Song Continues", "movie", "1986",
                     "Dramas", "TV-MA", "96 min", "United States",
                     "After their child was abducted and murdered, John and Reve Walsh fought to raise national awareness of the problem of missing children."));
    s.append(NfTitle("s310", "Addicted to Life", "movie", "2014",
                     "Action & Adventure,Documentaries,International Movies", "TV-14", "91 min", "France",
                     "Chasing extreme challenges, athletic daredevils test their limits in various environments from giant waves to snowy slopes around the world."));
    s.append(NfTitle("s311", "Adel Karam: Live from Beirut", "movie", "2018",
                     "Stand-Up Comedy", "TV-MA", "59 min", "United States",
                     "From Casino du Liban, Lebanese actor and comedian Adel Karam delivers earthy punchlines on kissing norms, colonoscopies and a porn star named Rocco."));
    s.append(NfTitle("s312", "Adhugo", "movie", "2018",
                     "Comedies,International Movies,Thrillers", "TV-14", "105 min", "India",
                     "A fast-footed piglet named Bunty becomes an object of desire for rival criminals that want the swift swine for their profitable animal-racing schemes."));
    s.append(NfTitle("s313", "Aditi Mittal: Things They Wouldn't Let Me Say", "movie", "2017",
                     "Stand-Up Comedy", "TV-MA", "63 min", "India",
                     "Trailblazing comic Aditi Mittal mixes topical stand-up with frank talk about being single, wearing thongs and the awkwardness of Indian movie ratings."));
    s.append(NfTitle("s314", "Adore", "movie", "2013",
                     "Dramas,Independent Movies,International Movies", "R", "111 min", "Australia, France",
                     "When lifelong friends Roz and Lil fall in love with each other's teenage sons, they must carry out their affairs in relative secrecy."));
    s.append(NfTitle("s315", "Adrift", "movie", "2018",
                     "Action & Adventure,Dramas,Romantic Movies", "PG-13", "96 min", "Hong Kong, Iceland, United States",
                     "A young couple’s sailing adventure becomes a fight to survive when their yacht faces a catastrophic hurricane in this story based on true events."));
    s.append(NfTitle("s316", "Adrishya", "movie", "2017",
                     "Horror Movies,Independent Movies,International Movies", "TV-14", "99 min", "India",
                     "A family’s harmonious existence is interrupted when the young son begins showing symptoms of anxiety that seem linked to disturbing events at home."));
    s.append(NfTitle("s317", "Advantageous", "movie", "2015",
                     "Dramas,Independent Movies,Sci-Fi & Fantasy", "TV-MA", "92 min", "United States",
                     "In order to keep her job at a biomedical engineering firm that prizes youth, a middle-aged woman must submit to a drastic experimental procedure."));
    s.append(NfTitle("s318", "Adventures in Public School", "movie", "2018",
                     "Comedies", "NR", "86 min", "Canada, United States",
                     "After years of home-schooling, an awkward teen decides he wants a public high school experience, but his overbearing mother struggles to let go."));
    s.append(NfTitle("s319", "Aerials", "movie", "2016",
                     "International Movies,Sci-Fi & Fantasy", "TV-14", "90 min", "United Arab Emirates",
                     "Dubai residents struggle to figure out why a fleet of alien spaceships are hovering over their city when all contact with the outside world is cut off."));
    s.append(NfTitle("s320", "Afflicted", "series", "2018",
                     "Docuseries,Reality TV,Science & Nature TV", "TV-MA", "1 Season", "United States",
                     "Baffling symptoms. Controversial diagnoses. Costly treatments. Seven people with chronic illnesses search for answers – and relief."));
    s.append(NfTitle("s321", "Afonso Padilha: Classless", "movie", "2020",
                     "Stand-Up Comedy", "TV-MA", "63 min", "Brazil",
                     "Brazilian comedian Afonso Padilha dives into his humble beginnings and digs out hilarious stories about his childhood in this very personal set."));
    s.append(NfTitle("s322", "Africa", "series", "2013",
                     "British TV Shows,Docuseries,International TV Shows", "TV-PG", "1 Season", "United Kingdom",
                     "This five-part nature series chronicles fascinating stories of survival on the African continent, home to the most diverse animal life on the planet."));
    s.append(NfTitle("s323", "Afronta! Facing It!", "series", "2017",
                     "Docuseries,International TV Shows", "TV-PG", "1 Season", "Brazil",
                     "This docuseries spotlights Afro-Brazilian thinkers sharing their individual journeys and discussing representation, entrepreneurship and community."));
    s.append(NfTitle("s324", "After", "movie", "2019",
                     "Dramas,Romantic Movies", "PG-13", "106 min", "United States",
                     "Wholesome college freshman Tessa Young thinks she knows what she wants out of life, until she crosses paths with complicated bad boy Hardin Scott."));
    s.append(NfTitle("s325", "After Life", "series", "2020",
                     "British TV Shows,International TV Shows,TV Comedies", "TV-MA", "2 Seasons", "United Kingdom",
                     "Struggling to come to terms with his wife's death, a writer for a newspaper adopts a gruff new persona in an effort to push away those trying to help."));
    s.append(NfTitle("s326", "After Maria", "movie", "2019",
                     "Documentaries", "TV-PG", "38 min", "United States",
                     "Displaced by Hurricane Maria, three Puerto Rican women navigate their families' uncertain futures as their federal housing aid in New York expires."));
    s.append(NfTitle("s327", "After Porn Ends", "movie", "2012",
                     "Documentaries", "NR", "93 min", "United States",
                     "This documentary explores the careers of some of porn's top stars and examines their adjustment to \"normal\" society after leaving adult entertainment."));
    s.append(NfTitle("s328", "After Porn Ends 3", "movie", "2018",
                     "Documentaries", "TV-MA", "87 min", "United States",
                     "This third installment in a documentary series examines the lives of the adult film genre's biggest stars after their industry careers have ended."));
    s.append(NfTitle("s329", "After the Raid", "movie", "2019",
                     "Documentaries,International Movies", "TV-PG", "25 min", "United States, Mexico",
                     "A large immigration raid in a small town leaves emotional fallout and hard questions for its churchgoers about what it means to love thy neighbor."));
    s.append(NfTitle("s330", "After We Collided", "movie", "2020",
                     "Dramas,Romantic Movies", "R", "105 min", "United States",
                     "Tessa fell hard and fast for Hardin, but after a betrayal tears them apart, she must decide whether to move on — or trust him with a second chance."));
    s.append(NfTitle("s331", "Aftermath", "movie", "2017",
                     "Dramas", "R", "94 min", "United Kingdom, United States",
                     "After an air traffic controller's mistake results in a tragic accident, a man who lost his wife and daughter seeks answers from the man responsible."));
    s.append(NfTitle("s332", "Afterschool", "movie", "2008",
                     "Dramas,Independent Movies", "TV-MA", "107 min", "United States",
                     "When a prep school loner films two classmates overdosing on cocaine, his footage plays a role in the emotional fallout within the school community."));
    s.append(NfTitle("s333", "Aftershock", "movie", "2012",
                     "Action & Adventure,Horror Movies", "R", "89 min", "United States, Chile",
                     "An American tourist and his friends are partying in Chile, but a major earthquake devastates the area and forces them to scramble for survival."));
    s.append(NfTitle("s334", "Against the Tide", "series", "2014",
                     "Crime TV Shows,International TV Shows,TV Dramas", "TV-14", "1 Season", "Singapore",
                     "A detective and a psychologist investigating a string of murders form a crime-solving team with the novelist whose work inspired the killings."));
    s.append(NfTitle("s335", "Agatha and the Truth of Murder", "movie", "2018",
                     "Dramas", "TV-MA", "92 min", "United Kingdom",
                     "In a dramatized depiction of her 11-day disappearance, novelist Agatha Christie solving a real murder amid a crisis in her writing and marriage."));
    s.append(NfTitle("s336", "Age Gap Love", "series", "2017",
                     "British TV Shows,Reality TV,Romantic TV Shows", "TV-MA", "1 Season", "United Kingdom",
                     "Despite the social backlash and challenges that come with intergenerational romance, these couples choose to stick together through thick and thin."));
    s.append(NfTitle("s337", "Age of Glory", "series", "2010",
                     "International TV Shows,Romantic TV Shows,TV Dramas", "TV-MA", "2 Seasons", "",
                     "Amid the thriving nightlife of 1960s Kuala Lumpur, three Chinese friends – a showgirl, her housekeeper and a chanteuse – find love and heartbreak."));
    s.append(NfTitle("s338", "Age of Rebellion", "series", "2018",
                     "Crime TV Shows,International TV Shows,TV Dramas", "TV-MA", "1 Season", "Taiwan",
                     "At their high school, a group of unruly teens wreak havoc, face bullies and navigate turbulent lives beyond school grounds."));
    s.append(NfTitle("s339", "Age of Tanks", "series", "2017",
                     "Docuseries,International TV Shows,Science & Nature TV", "TV-PG", "1 Season", "Germany, France, Russia",
                     "The history of military tanks unfolds in a documentary series that traces their role in history and geopolitics from World War I to the 21st century."));
    s.append(NfTitle("s340", "Agent", "series", "2019",
                     "International TV Shows,TV Dramas", "TV-MA", "1 Season", "Mauritius, South Africa",
                     "A former footballer tries to make it as a player agent in the world of African soccer, but a secret from his past threatens to destroy everything."));
    s.append(NfTitle("s341", "Agent Raghav", "series", "2015",
                     "Crime TV Shows,International TV Shows,TV Action & Adventure", "TV-14", "1 Season", "India",
                     "A mix of brilliance, erudition and skill enables a modern Sherlock Holmes to solve impossible cases, all while he tries to heal personal wounds."));
    s.append(NfTitle("s342", "Aggretsuko", "series", "2020",
                     "Anime Series,International TV Shows", "TV-14", "3 Seasons", "Japan",
                     "Frustrated with her thankless office job, Retsuko the Red Panda copes with her daily struggles by belting out death metal karaoke after work."));
    s.append(NfTitle("s343", "Aggretsuko: We Wish You a Metal Christmas", "movie", "2018",
                     "Movies", "TV-PG", "22 min", "Japan",
                     "While Retsuko desperately makes plans for Christmas Eve, her new obsession with seeking validation through social media spirals out of control."));
    s.append(NfTitle("s344", "Agneepath", "movie", "1990",
                     "Dramas,International Movies", "TV-14", "174 min", "India",
                     "A boy grows up to become a gangster in pursuit of the mobster who killed his innocent father, but revenge and reparation may come at great costs."));
    s.append(NfTitle("s345", "Agustín Aristarán: Soy Rada", "movie", "2018",
                     "Stand-Up Comedy", "TV-MA", "60 min", "Argentina",
                     "Argentine comedian Agustín \"Radagast\" Aristarán adds doses of magic, music and acting to his high-energy stand-up routine."));
    s.append(NfTitle("s346", "Agyaat", "movie", "2009",
                     "Horror Movies,International Movies", "TV-14", "97 min", "India",
                     "A film crew shooting in a remote forested location begins losing members one by one to a mysterious entity that they cannot see or hear."));
    s.append(NfTitle("s347", "Ahista Ahista", "movie", "2006",
                     "Dramas,International Movies,Romantic Movies", "TV-14", "122 min", "India",
                     "Stood up by her groom at the altar, a young woman finds help and healing from another man – until her ex-fiancé returns, hoping to win her back."));
    s.append(NfTitle("s348", "Ai Weiwei: Never Sorry", "movie", "2012",
                     "Documentaries", "R", "91 min", "United States",
                     "Chinese artist and activist Ai Weiwei uses social media and art to inspire protests and suffers government persecution for his actions."));
    s.append(NfTitle("s349", "Ainori Love Wagon: African Journey", "series", "2019",
                     "International TV Shows,Reality TV,Romantic TV Shows", "TV-14", "1 Season", "Japan",
                     "To find love, seven strangers leave Japan and embark on a journey through the continent of Africa together. Challenges, adventure and romance await!"));
    s.append(NfTitle("s350", "Ainori Love Wagon: Asian Journey", "series", "2018",
                     "International TV Shows,Reality TV,Romantic TV Shows", "TV-MA", "2 Seasons", "Japan",
                     "Seven men and women board a pink bus in search of true love. On a journey through Asia with strangers, their goal is to return to Japan as a couple."));
    s.append(NfTitle("s351", "Ainsley Eats the Streets", "series", "2014",
                     "British TV Shows,Docuseries,International TV Shows", "TV-PG", "1 Season", "United Kingdom",
                     "Celebrity chef Ainsley Harriott embarks on a journey around the world to explore the relationship between local street foods and cultural identity."));
    s.append(NfTitle("s352", "Ainu Mosir", "movie", "2020",
                     "Dramas,International Movies", "TV-PG", "84 min", "United States, Japan",
                     "A sensitive Ainu teen searches for a spiritual connection with his recently deceased dad while navigating his indigenous identity in a changing world."));
    s.append(NfTitle("s353", "Airplane Mode", "movie", "2020",
                     "Comedies,International Movies,Romantic Movies", "TV-PG", "97 min", "United States",
                     "When Ana, an influencer, crashes her car while talking on the phone, she’s shipped to her grumpy grandfather's farm – and forced into a digital detox."));
    s.append(NfTitle("s354", "Aisa Yeh Jahaan", "movie", "2015",
                     "Dramas,Independent Movies,International Movies", "TV-14", "127 min", "India",
                     "During a vacation with her parents, away from her concrete urban existence, a young girl learns to appreciate the joys of connecting with nature."));
    s.append(NfTitle("s355", "Aitraaz", "movie", "2004",
                     "International Movies,Music & Musicals,Romantic Movies", "TV-14", "153 min", "India",
                     "A happily married business executive is forced to deal with an episode from his past that he had long thought was put behind him."));
    s.append(NfTitle("s356", "Aiyaary", "movie", "2018",
                     "Action & Adventure,Dramas,International Movies", "TV-MA", "158 min", "India",
                     "When his protégé goes rogue and poses a grave threat to the government he serves, a veteran officer of the Indian Army tries to stop him at all costs."));
    s.append(NfTitle("s357", "Aiyyaa", "movie", "2012",
                     "Comedies,International Movies,Music & Musicals", "TV-14", "151 min", "India",
                     "An outspoken, imaginative girl from a conservative family pursues her dream man while fending off the prospect of an arranged marriage."));
    s.append(NfTitle("s358", "AJ and the Queen", "series", "2020",
                     "TV Comedies,TV Dramas", "TV-14", "1 Season", "United States",
                     "While traveling across the country in a run-down RV, drag queen Ruby Red discovers an unlikely sidekick in AJ: a tough-talking 10-year-old stowaway."));
    s.append(NfTitle("s359", "Ajab Prem Ki Ghazab Kahani", "movie", "2009",
                     "Comedies,International Movies,Music & Musicals", "TV-14", "150 min", "India",
                     "A young man's obsession with making others happy drives him to help the girl he loves marry someone else, then try to win her heart."));
    s.append(NfTitle("s360", "Ajaibnya Cinta", "series", "2015",
                     "International TV Shows,TV Dramas", "TV-PG", "1 Season", "",
                     "Upon returning home from his studies abroad, young grad Akif finds himself involved with four women in this continuation of the series \"Spain Uoolss.\""));
    s.append(NfTitle("s361", "AJIN: Demi-Human", "series", "2016",
                     "Anime Series,International TV Shows", "TV-MA", "2 Seasons", "Japan",
                     "A teenager discovers that he is an Ajin and flees before the authorities experiment on him. Other Ajin plan to fight back and he must choose a side."));
    s.append(NfTitle("s362", "Ajji", "movie", "2017",
                     "Dramas,International Movies", "TV-MA", "105 min", "India",
                     "When her small granddaughter is sexually assaulted by a powerful man and no justice is served, a poor, frail old woman plots her violent revenge."));
    s.append(NfTitle("s363", "AK vs AK", "movie", "2020",
                     "Comedies,Dramas,International Movies", "TV-MA", "109 min", "India",
                     "After a public spat with a movie star, a disgraced director retaliates by kidnapping the actor’s daughter, filming the search for her in real time."));
    s.append(NfTitle("s364", "Akame ga Kill!", "series", "2014",
                     "Anime Series,Crime TV Shows,International TV Shows", "TV-14", "1 Season", "Japan",
                     "Tatsumi sets out on a journey to help his poor village. When he's rescued by a band of assassins, he joins their fight against the corrupt government."));
    s.append(NfTitle("s365", "Akbar Birbal", "series", "2019",
                     "Kids' TV,TV Comedies,TV Dramas", "TV-G", "1 Season", "India",
                     "From battles of wit to fights for justice, Emperor Akbar and his trusty advisor Birbal display their unique bond through adventures in Mughal-era India."));
    s.append(NfTitle("s366", "Akulah Balqis", "series", "2015",
                     "International TV Shows,TV Dramas", "TV-14", "1 Season", "",
                     "A little girl must enter an orphanage after her parents are arrested, but her life takes a dramatic turn when she escapes and meets a model."));
    s.append(NfTitle("s367", "Al acecho", "movie", "2019",
                     "International Movies,Thrillers", "TV-MA", "81 min", "",
                     "Looking for a fresh start, a park ranger gets a new assignment. When he discovers a network of poachers, survival depends on his lethal instincts."));
    s.append(NfTitle("s368", "Al Hayba", "series", "2017",
                     "Crime TV Shows,International TV Shows,TV Dramas", "TV-14", "1 Season", "Lebanon",
                     "In a village by the Lebanon-Syria border, the head of an arms-smuggling clan contends with family conflicts, power struggles and complicated love."));
    s.append(NfTitle("s369", "Ala Vaikunthapurramuloo", "movie", "2020",
                     "Action & Adventure,Comedies,Dramas", "TV-14", "162 min", "India",
                     "After growing up enduring criticism from his father, a young man finds his world shaken upon learning he was switched at birth with a millionaire’s son."));
    s.append(NfTitle("s370", "Alakada Reloaded", "movie", "2017",
                     "Comedies,International Movies", "TV-MA", "104 min", "",
                     "To cope with her feelings of inferiority, a woman from an impoverished family tells tall tales of her wealth and influence to upgrade her social status."));
    s.append(NfTitle("s371", "Alan Saldaña: Mi vida de pobre", "movie", "2017",
                     "Stand-Up Comedy", "TV-MA", "54 min", "Mexico",
                     "Mexican comic Alan Saldaña has fun with everything from the pressure of sitting in an exit row to maxing out his credit card in this stand-up special."));
    s.append(NfTitle("s372", "Alarmoty in the Land of Fire", "movie", "2017",
                     "Comedies,International Movies", "TV-MA", "94 min", "Egypt",
                     "While vacationing at a resort, an ornery and outspoken man is held captive by a criminal organization."));
    s.append(NfTitle("s373", "Alaska Is a Drag", "movie", "2017",
                     "Dramas,LGBTQ Movies", "TV-MA", "83 min", "United States",
                     "Tormented by bullies, an aspiring drag star working at an Alaskan cannery becomes a skilled fighter and is tapped for competition by a boxing coach."));
    s.append(NfTitle("s374", "Albert Pinto Ko Gussa Kyun Aata Hai?", "movie", "2019",
                     "Dramas,Independent Movies,International Movies", "TV-MA", "85 min", "India",
                     "As the police investigate his disappearance, a young man heads to Goa to carry out an act of vengeance fueled by years of pent-up anger toward society."));
    s.append(NfTitle("s375", "Albion: The Enchanted Stallion", "movie", "2016",
                     "Children & Family Movies", "TV-PG", "106 min", "United States, Bulgaria",
                     "After a magical horse transports her to a fantasy world ruled by an evil general, a brave girl sets out to save the land once and for all."));
    s.append(NfTitle("s376", "Alejandro Riaño: Especial de stand up", "movie", "2018",
                     "Stand-Up Comedy", "TV-MA", "60 min", "Colombia",
                     "Colombian comedian Alejandro Riaño discusses the perks of dating a she-wolf, styles of dancing, the quirks of Bogotá men and soccer game announcers."));
    s.append(NfTitle("s377", "Alejandro Sanz: What I Was Is What I Am", "movie", "2018",
                     "Documentaries,International Movies,Music & Musicals", "TV-MA", "102 min", "Spain",
                     "A retrospective look at the life and career of Grammy-award winning Spanish musician Alejandro Sanz."));
    s.append(NfTitle("s378", "Alelí", "movie", "2019",
                     "Comedies,Dramas,International Movies", "TV-MA", "88 min", "Uruguay, Argentina",
                     "Mourning their father's death, a dysfunctional trio of siblings must face selling their beloved childhood beach house — and dealing with each other."));
    s.append(NfTitle("s379", "Alex Fernández: The Best Comedian in the World", "movie", "2020",
                     "Stand-Up Comedy", "TV-MA", "51 min", "Mexico",
                     "Comic Alex Fernández performs his familiar autobiographical stories but goes a little deeper this time with a tender tale about one of his six siblings."));
    s.append(NfTitle("s380", "Alex Strangelove", "movie", "2018",
                     "Comedies,Dramas,Independent Movies", "TV-MA", "100 min", "United States",
                     "High school senior Alex Truelove's plan to lose his virginity to lovable girlfriend Claire goes awry when he meets the equally lovable Elliot."));
    s.append(NfTitle("s381", "Alexa & Katie", "series", "2020",
                     "Kids' TV,TV Comedies", "TV-G", "4 Seasons", "United States",
                     "Alexa is battling cancer. But with her best friend, Katie, by her side, she's also starting high school – and ready for whatever comes next."));
    s.append(NfTitle("s382", "Alexandria ... Why?", "movie", "1979",
                     "Classic Movies,Dramas,International Movies", "TV-MA", "131 min", "Egypt, Algeria",
                     "Living in Alexandria during World War II, an Egyptian teen enamored with American films dreams of making it in Hollywood."));
    s.append(NfTitle("s383", "Alexandria: Again and Forever", "movie", "1989",
                     "Classic Movies,Dramas,International Movies", "TV-MA", "109 min", "France, Egypt",
                     "At the peak of his career, Yehia joins a hunger strike, becomes smitten and reckons with a creative crisis — but finds a new muse."));
    s.append(NfTitle("s384", "Alexis Viera: A Story of Surviving", "movie", "2019",
                     "Documentaries,International Movies,Sports Movies", "TV-MA", "69 min", "Uruguay",
                     "After being shot during a robbery in Colombia and losing sensation in his legs, Uruguayan soccer star Alexis Viera finds a new sense of purpose."));
    s.append(NfTitle("s385", "Ali & Alia", "movie", "2019",
                     "Dramas,International Movies,Romantic Movies", "TV-14", "106 min", "United Arab Emirates",
                     "Drugs and addiction endanger the love — and lives — of two childhood sweethearts struggling to survive the perils of a precarious world."));
    s.append(NfTitle("s386", "Ali Baba ve 7 Cüceler", "movie", "2015",
                     "Action & Adventure,Comedies,International Movies", "TV-MA", "115 min", "Turkey",
                     "A garden gnome vendor and his brother-in-law attend a trade fair in Sofia, where they accidentally stumble into shenanigans involving a mafia boss."));
    s.append(NfTitle("s387", "Ali Wong: Baby Cobra", "movie", "2016",
                     "Stand-Up Comedy", "TV-MA", "60 min", "United States",
                     "Ali Wong's stand up special delves into her sexual adventures, hoarding, the rocky road to pregnancy, and why feminism is terrible."));
    s.append(NfTitle("s388", "Ali Wong: Hard Knock Wife", "movie", "2018",
                     "Stand-Up Comedy", "TV-MA", "64 min", "United States",
                     "Two years after the hit \"Baby Cobra,\" Ali Wong is back with another baby bump – and a torrent of hilarious truths about marriage and motherhood."));
    s.append(NfTitle("s389", "Ali's Wedding", "movie", "2018",
                     "Comedies,Independent Movies,International Movies", "TV-MA", "110 min", "Australia",
                     "After telling a white lie that spins out of control, the son of an Iraqi-born cleric in Melbourne becomes torn between family duty and his own heart."));
    s.append(NfTitle("s390", "Alias Grace", "series", "2017",
                     "Crime TV Shows,International TV Shows,TV Dramas", "TV-MA", "1 Season", "Canada",
                     "In 19th-century Canada, a psychiatrist weighs whether a murderess should be pardoned due to insanity. Based on Margaret Atwood's award-winning novel."));
    s.append(NfTitle("s391", "Alias JJ, la celebridad del mal", "movie", "2017",
                     "Documentaries,International Movies", "TV-MA", "54 min", "",
                     "Witnesses and public figures respond to controversial claims about Pablo Escobar made by the Medellín Cartel's sole survivor, John Jairo Velásquez."));
    s.append(NfTitle("s392", "Alibaba Aur 40 Chor", "movie", "1980",
                     "Action & Adventure,International Movies,Sci-Fi & Fantasy", "TV-14", "147 min", "Soviet Union, India",
                     "A simple village man is thrown into a web of political intrigue after he takes on a powerful and ruthless gang of bandits."));
    s.append(NfTitle("s393", "Alice Doesn't Live Here Anymore", "movie", "1974",
                     "Classic Movies,Comedies,Dramas", "PG", "112 min", "United States",
                     "A widowed singer and single mother starts over as a diner waitress in Arizona, befriending her coworkers and romancing a ruggedly handsome rancher."));
    s.append(NfTitle("s394", "Alice in Borderland", "series", "2020",
                     "International TV Shows,TV Action & Adventure,TV Mysteries", "TV-MA", "1 Season", "Japan",
                     "An aimless gamer and his two friends find themselves in a parallel Tokyo, where they're forced to compete in a series of sadistic games to survive."));
    s.append(NfTitle("s395", "Alice Junior", "movie", "2019",
                     "Comedies,International Movies,LGBTQ Movies", "TV-MA", "87 min", "Brazil",
                     "In a small town, a trans teen with a vibrant personality shakes up her high school's conservative ways while trying to secure her first kiss."));
    s.append(NfTitle("s396", "Alien Contact: Outer Space", "movie", "2017",
                     "Documentaries", "TV-PG", "63 min", "United States",
                     "This fact-based account delves into humankind's efforts to gather signals from possible intelligent beings beyond the solar system."));
    s.append(NfTitle("s397", "Alien TV", "series", "2020",
                     "Kids' TV,TV Comedies", "TV-Y7", "1 Season", "",
                     "Alien reporters Ixbee, Pixbee and Squee travel to a lovely but odd planet called Earth, where they attempt to make sense of humans and their hobbies."));
    s.append(NfTitle("s398", "Alien Warfare", "movie", "2019",
                     "Action & Adventure,Sci-Fi & Fantasy", "TV-MA", "88 min", "United States",
                     "The U.S. Navy Seal team tackles a top-secret mission at a research center where scientists have mysteriously disappeared and another life form awaits."));
    s.append(NfTitle("s399", "Alien Worlds", "series", "2020",
                     "British TV Shows,Docuseries,International TV Shows", "TV-PG", "1 Season", "United Kingdom",
                     "Applying the laws of life on Earth to the rest of the galaxy, this series blends science fact and fiction to imagine alien life on other planets."));
    s.append(NfTitle("s400", "Alien Xmas", "movie", "2020",
                     "Children & Family Movies", "TV-Y", "42 min", "United States",
                     "A young elf mistakes a tiny alien for a Christmas gift, not knowing her new plaything has plans to destroy Earth's gravity — and steal all the presents."));
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
