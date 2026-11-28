// SPDX-License-Identifier: MIT
#include "models/emoji_catalog.h"

#include <QSettings>
#include <QVariantMap>

namespace Zalos::Core::Models {

namespace {

enum CategoryIndex : quint8 {
    CatZalo = 0,
    CatSmileys,
    CatPeople,
    CatAnimals,
    CatFood,
    CatTravel,
    CatActivities,
    CatObjects,
    CatSymbols,
    CatFlags,
};

struct CategoryDef {
    const char* id;
    const char* glyph;
    const char* nameVi;
    const char* nameEn;
};

struct EmojiDef {
    CategoryIndex cat;
    const char* glyph;
    const char* code;
    const char* keywords;
};

constexpr CategoryDef kCategories[] = {
    {"recent",     "clock", "Gần đây",              "Recent"},
    {"zalo",       "😄",    "Zalo cảm xúc",         "Zalo Emoji"},
    {"smileys",    "😀",    "Mặt cười & Cảm xúc",   "Smileys & Emotion"},
    {"people",     "👋",    "Con người & Cử chỉ",   "People & Body"},
    {"animals",    "🐾",    "Động vật & Thiên nhiên","Animals & Nature"},
    {"food",       "🍔",    "Đồ ăn & Đồ uống",      "Food & Drink"},
    {"travel",     "✈️",    "Du lịch & Địa điểm",   "Travel & Places"},
    {"activities", "⚽",    "Hoạt động & Thể thao", "Activities"},
    {"objects",    "💡",    "Đồ vật & Công việc",   "Objects"},
    {"symbols",    "❤️",    "Ký hiệu & Biểu tượng", "Symbols"},
    {"flags",      "🇻🇳",   "Cờ các quốc gia",      "Flags"},
};

constexpr EmojiDef kEmojis[] = {
    {CatZalo, "😊", ":)",         "cuoi mim smile zalo"},
    {CatZalo, "😄", ":d",         "cuoi tuoi grin haha zalo"},
    {CatZalo, "😂", ":')",        "cuoi ra nuoc mat joy haha zalo"},
    {CatZalo, "😘", ":-*",        "hon gio kiss love zalo"},
    {CatZalo, "❤️", "/-heart",    "tim yeu heart love zalo"},
    {CatZalo, "👍", "/-strong",   "thich like tot strong zalo"},
    {CatZalo, "👎", "--b",        "khong thich dislike bad zalo"},
    {CatZalo, "😗", ":3",         "chu mo kiss cute zalo"},
    {CatZalo, "😛", ":b",         "le luoi tongue zalo"},
    {CatZalo, "😜", ";d",         "nhay mat le luoi wink tongue zalo"},
    {CatZalo, "🥴", ":~",         "say choang dizzy woozy zalo"},
    {CatZalo, "😏", ":>",         "nham hiem smirk zalo"},
    {CatZalo, "😝", ";p",         "le luoi nham mat squint tongue zalo"},
    {CatZalo, "😚", ":*",         "hon kiss zalo"},
    {CatZalo, "😮", ";o",         "ngac nhien surprised wow zalo"},
    {CatZalo, "😭", ":((",        "khoc lon sob cry zalo"},
    {CatZalo, "😜", ":p",         "le luoi playful zalo"},
    {CatZalo, "😳", ":$",         "xau ho ngai blush flushed zalo"},
    {CatZalo, "👋", ":-h",        "vay tay chao wave hi zalo"},
    {CatZalo, "😢", ":-((",       "buon khoc sad cry zalo"},
    {CatZalo, "😆", "x-)",        "cuoi tit mat laugh xd zalo"},
    {CatZalo, "😎", "8-)",        "ngau kinh ram cool sunglasses zalo"},
    {CatZalo, "🤪", ";-d",        "tinh nghich crazy zalo"},
    {CatZalo, "😣", ":q",         "kho chiu grimace zalo"},
    {CatZalo, "🙁", ":(",         "buon sad frown zalo"},
    {CatZalo, "😎", "b-)",        "kinh mat ngau cool zalo"},
    {CatZalo, "🤔", ";?",         "suy nghi thac mac think hmm zalo"},
    {CatZalo, "😐", ":|",         "vo cam neutral zalo"},
    {CatZalo, "😵", ";xx",        "chong mat choang dizzy zalo"},
    {CatZalo, "😑", ":--|",       "chan nan expressionless zalo"},
    {CatZalo, "😬", ";g",         "nhan mat grimace zalo"},
    {CatZalo, "😲", ":o",         "sung sot astonished zalo"},
    {CatZalo, "😴", ":z",         "buon ngu sleep zzz zalo"},
    {CatZalo, "😒", ":l",         "chan khong hai long unamused zalo"},
    {CatZalo, "😩", "p-(",        "met moi weary zalo"},
    {CatZalo, "🙋", ":-bye",      "tam biet bye zalo"},
    {CatZalo, "🤐", ":x",         "im lang giu bi mat zip zalo"},
    {CatZalo, "🥱", "|-)",        "ngap buon ngu yawn zalo"},
    {CatZalo, "😅", ":wipe",      "lau mo hoi ngai sweat zalo"},
    {CatZalo, "😱", ":!",         "hot hoang scream zalo"},
    {CatZalo, "🤩", "8*",         "mat sao nguong mo star struck zalo"},
    {CatZalo, "🤨", ":-dig",      "nghi ngo raised eyebrow zalo"},
    {CatZalo, "😤", ":t",         "tuc gian huff zalo"},
    {CatZalo, "😠", "&-(",        "cau co angry zalo"},
    {CatZalo, "😉", ";-)",        "nhay mat wink zalo"},
    {CatZalo, "👏", ":handclap",  "vo tay clap zalo"},
    {CatZalo, "🙄", ">-|",        "dao mat roll eyes zalo"},
    {CatZalo, "😪", ":-f",        "met buon ngu sleepy zalo"},
    {CatZalo, "😔", ":-l",        "tram tu pensive zalo"},
    {CatZalo, "😕", ":-r",        "boi roi confused zalo"},
    {CatZalo, "😖", ";-/",        "boi roi confounded zalo"},
    {CatZalo, "🤬", ";-x",        "gian du swear zalo"},
    {CatZalo, "🤯", ":-o",        "no nao mind blown zalo"},
    {CatZalo, "🤢", ";-s",        "buon non nauseated zalo"},
    {CatZalo, "🤑", "8*)",        "me tien money zalo"},
    {CatZalo, "😰", ";!",         "lo lang anxious zalo"},
    {CatZalo, "🥵", ";f",         "nong buc hot zalo"},
    {CatZalo, "🥶", ":;",         "lanh run cold zalo"},
    {CatZalo, "✌️", ":v",         "hai ngon pac man peace zalo"},
    {CatZalo, "🥳", ";-a",        "tiec tung party zalo"},
    {CatZalo, "🥺", ":-<",        "nan ni pleading zalo"},
    {CatZalo, "🤣", ":))",        "cuoi lan lon rofl haha zalo"},
    {CatZalo, "🤩", "$-)",        "lap lanh excited zalo"},
    {CatZalo, "🤕", ";-!",        "dau thuong bandage zalo"},
    {CatZalo, "🤟", "/-showlove", "yeu thuong love you zalo"},
    {CatZalo, "🍺", "/-beer",     "bia dzo beer cheers zalo"},
    {CatZalo, "☕", "/-coffee",   "ca phe coffee zalo"},
    {CatZalo, "🌹", "/-rose",     "hoa hong rose flower zalo"},
    {CatZalo, "🥀", "/-fade",     "hoa heo wilted zalo"},
    {CatZalo, "🎂", "/-bd",       "sinh nhat birthday cake zalo"},
    {CatZalo, "💣", "/-bome",     "bom no bomb zalo"},
    {CatZalo, "🍰", "/-cake",     "banh ngot cake zalo"},
    {CatZalo, "💔", "/-break",    "tan vo heartbreak zalo"},
    {CatZalo, "💩", "/-shit",     "cut poop zalo"},
    {CatZalo, "⚡", "/-li",       "set nhanh lightning zalo"},
    {CatZalo, "👎", "/-weak",     "yeu kem weak thumbs down zalo"},
    {CatZalo, "👌", "/-ok",       "ok duoc zalo"},
    {CatZalo, "✌️", "/-v",        "chien thang victory zalo"},
    {CatZalo, "🙏", "/-thanks",   "cam on thanks pray zalo"},
    {CatZalo, "👊", "/-punch",    "dam fist punch zalo"},
    {CatZalo, "🤝", "/-share",    "bat tay hop tac handshake zalo"},
    {CatZalo, "🙇", "_()_",       "cui dau xin loi bow sorry zalo"},
    {CatZalo, "🙅", "/-no",       "khong tu choi no zalo"},
    {CatZalo, "👎", "/-bad",      "te bad zalo"},
    {CatZalo, "🫶", "/-loveu",    "tha tim yeu love hands zalo"},
    {CatZalo, "🚩", "/-flag",     "co flag zalo"},
    {CatZalo, "🎉", "/-jj",       "chuc mung phao hoa party tada zalo"},

    {CatSmileys, "😀", "", "mat cuoi grinning happy"},
    {CatSmileys, "😃", "", "cuoi tuoi smiley happy"},
    {CatSmileys, "😄", "", "cuoi lon smile happy"},
    {CatSmileys, "😁", "", "cuoi nhe rang grin"},
    {CatSmileys, "😆", "", "cuoi tit mat laughing"},
    {CatSmileys, "😅", "", "cuoi ra mo hoi sweat smile"},
    {CatSmileys, "🤣", "", "cuoi lan lon rofl"},
    {CatSmileys, "😂", "", "cuoi ra nuoc mat joy"},
    {CatSmileys, "🙂", "", "cuoi nhe slightly smiling"},
    {CatSmileys, "🙃", "", "mat nguoc upside down"},
    {CatSmileys, "🫠", "", "tan chay melting"},
    {CatSmileys, "😉", "", "nhay mat wink"},
    {CatSmileys, "😊", "", "mim cuoi blush"},
    {CatSmileys, "😇", "", "thien than innocent halo"},
    {CatSmileys, "🥰", "", "hanh phuc yeu thuong hearts"},
    {CatSmileys, "😍", "", "mat trai tim heart eyes love"},
    {CatSmileys, "🤩", "", "mat ngoi sao star struck"},
    {CatSmileys, "😘", "", "hon gio kiss heart"},
    {CatSmileys, "😗", "", "hon kissing"},
    {CatSmileys, "☺️", "", "am ap relaxed"},
    {CatSmileys, "😚", "", "hon nham mat kissing closed eyes"},
    {CatSmileys, "😙", "", "hon cuoi kissing smiling"},
    {CatSmileys, "🥲", "", "cuoi trong nuoc mat tear"},
    {CatSmileys, "😋", "", "ngon mieng yum"},
    {CatSmileys, "😛", "", "le luoi stuck out tongue"},
    {CatSmileys, "😜", "", "nhay mat le luoi winking tongue"},
    {CatSmileys, "🤪", "", "tinh nghich zany"},
    {CatSmileys, "😝", "", "le luoi squint tongue"},
    {CatSmileys, "🤑", "", "tien money mouth"},
    {CatSmileys, "🤗", "", "om hugging"},
    {CatSmileys, "🤭", "", "che mieng cuoi hand over mouth"},
    {CatSmileys, "🫢", "", "ngac nhien che mieng"},
    {CatSmileys, "🫣", "", "nhin trom peeking"},
    {CatSmileys, "🤫", "", "suyt im lang shush"},
    {CatSmileys, "🤔", "", "suy nghi thinking"},
    {CatSmileys, "🫡", "", "chao dieu lenh salute"},
    {CatSmileys, "🤐", "", "khoa mieng zipper"},
    {CatSmileys, "🤨", "", "nhuong may raised eyebrow"},
    {CatSmileys, "😐", "", "vo cam neutral"},
    {CatSmileys, "😑", "", "chan nan expressionless"},
    {CatSmileys, "😶", "", "khong mieng no mouth"},
    {CatSmileys, "🫥", "", "mo nhat dotted line"},
    {CatSmileys, "😏", "", "nham hiem smirk"},
    {CatSmileys, "😒", "", "kho chiu unamused"},
    {CatSmileys, "🙄", "", "dao mat eye roll"},
    {CatSmileys, "😬", "", "nhan mat grimacing"},
    {CatSmileys, "😮‍💨", "", "tho dai exhaling"},
    {CatSmileys, "🤥", "", "noi doi lying pinocchio"},
    {CatSmileys, "😌", "", "nhe nhom relieved"},
    {CatSmileys, "😔", "", "buon ba pensive"},
    {CatSmileys, "😪", "", "buon ngu sleepy"},
    {CatSmileys, "🤤", "", "chay nuoc mieng drooling"},
    {CatSmileys, "😴", "", "ngu ngon sleeping"},
    {CatSmileys, "😷", "", "khau trang mask sick"},
    {CatSmileys, "🤒", "", "sot cap nhiet ke thermometer"},
    {CatSmileys, "🤕", "", "bang bo head bandage"},
    {CatSmileys, "🤢", "", "buon non nauseated"},
    {CatSmileys, "🤮", "", "non mua vomiting"},
    {CatSmileys, "🤧", "", "hat hoi sneezing"},
    {CatSmileys, "🥵", "", "nong buc hot"},
    {CatSmileys, "🥶", "", "lanh cong cold"},
    {CatSmileys, "🥴", "", "say choang woozy"},
    {CatSmileys, "😵", "", "chong mat dizzy"},
    {CatSmileys, "😵‍💫", "", "xoay vong spiral eyes"},
    {CatSmileys, "🤯", "", "no nao exploding head"},
    {CatSmileys, "🤠", "", "cao boi cowboy"},
    {CatSmileys, "🥳", "", "tiec tung partying"},
    {CatSmileys, "🥸", "", "cai trang disguised"},
    {CatSmileys, "😎", "", "ngau sunglasses cool"},
    {CatSmileys, "🤓", "", "mot sach nerd glasses"},
    {CatSmileys, "🧐", "", "soi xet monocle"},
    {CatSmileys, "😕", "", "boi roi confused"},
    {CatSmileys, "🫤", "", "nghieng mieng diagonal"},
    {CatSmileys, "😟", "", "lo lang worried"},
    {CatSmileys, "🙁", "", "buon slightly frowning"},
    {CatSmileys, "☹️", "", "rat buon frowning"},
    {CatSmileys, "😮", "", "o mieng open mouth"},
    {CatSmileys, "😯", "", "im lang ngac nhien hushed"},
    {CatSmileys, "😲", "", "sung sot astonished"},
    {CatSmileys, "😳", "", "do mat flushed"},
    {CatSmileys, "🥺", "", "nan ni long lanh pleading"},
    {CatSmileys, "🥹", "", "xuc dong holding back tears"},
    {CatSmileys, "😦", "", "nhau may frowning open"},
    {CatSmileys, "😧", "", "dau kho anguished"},
    {CatSmileys, "😨", "", "so hai fearful"},
    {CatSmileys, "😰", "", "toat mo hoi cold sweat"},
    {CatSmileys, "😥", "", "nhe nhom buon disappointed relieved"},
    {CatSmileys, "😢", "", "khoc cry"},
    {CatSmileys, "😭", "", "khoc loc sob"},
    {CatSmileys, "😱", "", "het so hai scream"},
    {CatSmileys, "😖", "", "kho so confounded"},
    {CatSmileys, "😣", "", "kien tri chiu dung persevere"},
    {CatSmileys, "😞", "", "that vong disappointed"},
    {CatSmileys, "😓", "", "met moi downcast sweat"},
    {CatSmileys, "😩", "", "met la weary"},
    {CatSmileys, "😫", "", "kiet suc tired"},
    {CatSmileys, "🥱", "", "ngap yawning"},
    {CatSmileys, "😤", "", "tuc gian triumph"},
    {CatSmileys, "😡", "", "gian du rage angry"},
    {CatSmileys, "😠", "", "tuc toi angry"},
    {CatSmileys, "🤬", "", "chui the symbols mouth"},
    {CatSmileys, "😈", "", "ac quy cuoi smiling imp"},
    {CatSmileys, "👿", "", "ac quy gian angry imp"},
    {CatSmileys, "💀", "", "dau lau skull"},
    {CatSmileys, "☠️", "", "nguy hiem skull crossbones"},
    {CatSmileys, "💩", "", "phan poop"},
    {CatSmileys, "🤡", "", "chu he clown"},
    {CatSmileys, "👹", "", "quy ogre"},
    {CatSmileys, "👺", "", "yeu tinh goblin"},
    {CatSmileys, "👻", "", "con ma ghost"},
    {CatSmileys, "👽", "", "nguoi ngoai hanh tinh alien"},
    {CatSmileys, "👾", "", "quai vat game space invader"},
    {CatSmileys, "🤖", "", "nguoi may robot"},
    {CatSmileys, "😺", "", "meo cuoi smiley cat"},
    {CatSmileys, "😸", "", "meo grin cat"},
    {CatSmileys, "😹", "", "meo cuoi ra nuoc mat joy cat"},
    {CatSmileys, "😻", "", "meo mat tim heart eyes cat"},
    {CatSmileys, "😼", "", "meo nham hiem smirk cat"},
    {CatSmileys, "😽", "", "meo hon kissing cat"},
    {CatSmileys, "🙀", "", "meo so hai weary cat"},
    {CatSmileys, "😿", "", "meo khoc crying cat"},
    {CatSmileys, "😾", "", "meo gian pouting cat"},
    {CatSmileys, "🙈", "", "khi che mat see no evil"},
    {CatSmileys, "🙉", "", "khi bit tai hear no evil"},
    {CatSmileys, "🙊", "", "khi bit mieng speak no evil"},
    {CatSmileys, "💋", "", "dau son moi kiss"},
    {CatSmileys, "💌", "", "thu tinh love letter"},
    {CatSmileys, "💘", "", "trai tim mui ten cupid"},
    {CatSmileys, "💝", "", "trai tim qua ribbon"},
    {CatSmileys, "💖", "", "trai tim lap lanh sparkling heart"},
    {CatSmileys, "💗", "", "trai tim dap growing heart"},
    {CatSmileys, "💓", "", "tim dap beating heart"},
    {CatSmileys, "💞", "", "hai trai tim revolving hearts"},
    {CatSmileys, "💕", "", "doi trai tim two hearts"},
    {CatSmileys, "💟", "", "trang tri trai tim heart decoration"},
    {CatSmileys, "❣️", "", "trai tim cham than"},
    {CatSmileys, "💔", "", "trai tim tan vo broken heart"},
    {CatSmileys, "❤️‍🔥", "", "trai tim ruc chay heart on fire"},
    {CatSmileys, "💯", "", "tram diem 100"},
    {CatSmileys, "💢", "", "tuc gian anger"},
    {CatSmileys, "💥", "", "no tung boom collision"},
    {CatSmileys, "💫", "", "chong mat ngoi sao dizzy"},
    {CatSmileys, "💦", "", "giot mo hoi sweat drops"},
    {CatSmileys, "💨", "", "chay nhanh dash"},
    {CatSmileys, "🕳️", "", "ho den hole"},
    {CatSmileys, "💬", "", "bong bong chat speech balloon"},
    {CatSmileys, "💭", "", "suy nghi thought balloon"},
    {CatSmileys, "💤", "", "ngu zzz"},

    {CatPeople, "👋", "", "vay tay wave hello"},
    {CatPeople, "🤚", "", "gio tay raised back of hand"},
    {CatPeople, "🖐️", "", "xoe ban tay hand"},
    {CatPeople, "✋", "", "dung lai stop high five"},
    {CatPeople, "🖖", "", "vulcan salute"},
    {CatPeople, "🫱", "", "dua tay phai"},
    {CatPeople, "🫲", "", "dua tay trai"},
    {CatPeople, "🫳", "", "up tay"},
    {CatPeople, "🫴", "", "ngua tay"},
    {CatPeople, "👌", "", "ok duoc tuyet"},
    {CatPeople, "🤌", "", "chum ngon tay pinched"},
    {CatPeople, "🤏", "", "mot chut pinching"},
    {CatPeople, "✌️", "", "chien thang hai ngon peace victory"},
    {CatPeople, "🤞", "", "cau may crossed fingers"},
    {CatPeople, "🫰", "", "ban tim finger heart"},
    {CatPeople, "🤟", "", "yeu ban love you"},
    {CatPeople, "🤘", "", "rock on horns"},
    {CatPeople, "🤙", "", "goi dien call me shaka"},
    {CatPeople, "👈", "", "chi trai point left"},
    {CatPeople, "👉", "", "chi phai point right"},
    {CatPeople, "👆", "", "chi len point up"},
    {CatPeople, "🖕", "", "ngon giua"},
    {CatPeople, "👇", "", "chi xuong point down"},
    {CatPeople, "☝️", "", "mot ngon chi len"},
    {CatPeople, "🫵", "", "chi ban point at you"},
    {CatPeople, "👍", "", "thich like ok tuyet"},
    {CatPeople, "👎", "", "khong thich dislike"},
    {CatPeople, "✊", "", "nam dam fist"},
    {CatPeople, "👊", "", "cu dam punch"},
    {CatPeople, "🤛", "", "dam trai left fist"},
    {CatPeople, "🤜", "", "dam phai right fist"},
    {CatPeople, "👏", "", "vo tay clap"},
    {CatPeople, "🙌", "", "gio hai tay hoan ho"},
    {CatPeople, "🫶", "", "tha tim heart hands"},
    {CatPeople, "👐", "", "mo tay open hands"},
    {CatPeople, "🤲", "", "hung tay palms up"},
    {CatPeople, "🤝", "", "bat tay hop tac handshake"},
    {CatPeople, "🙏", "", "cam on cau nguyen pray thanks"},
    {CatPeople, "✍️", "", "viet tay writing"},
    {CatPeople, "💅", "", "lam mong nail"},
    {CatPeople, "🤳", "", "chup anh selfie"},
    {CatPeople, "💪", "", "co bap manh me muscle flex"},
    {CatPeople, "🦾", "", "tay robot mechanical arm"},
    {CatPeople, "🦵", "", "chan leg"},
    {CatPeople, "🦶", "", "ban chan foot"},
    {CatPeople, "👂", "", "tai ear"},
    {CatPeople, "👃", "", "mui nose"},
    {CatPeople, "🧠", "", "nao brain"},
    {CatPeople, "👀", "", "doi mat nhin eyes"},
    {CatPeople, "👁️", "", "con mat eye"},
    {CatPeople, "👅", "", "luoi tongue"},
    {CatPeople, "👄", "", "moi mouth"},
    {CatPeople, "👶", "", "em be baby"},
    {CatPeople, "🧒", "", "tre em child"},
    {CatPeople, "👦", "", "be trai boy"},
    {CatPeople, "👧", "", "be gai girl"},
    {CatPeople, "🧑", "", "nguoi person"},
    {CatPeople, "👨", "", "dan ong man"},
    {CatPeople, "👩", "", "phu nu woman"},
    {CatPeople, "🧓", "", "nguoi cao tuoi older"},
    {CatPeople, "👴", "", "ong gia old man"},
    {CatPeople, "👵", "", "ba gia old woman"},
    {CatPeople, "🙍", "", "buon bieu cam"},
    {CatPeople, "🙎", "", "doi pouting"},
    {CatPeople, "🙅", "", "tu choi khong no"},
    {CatPeople, "🙆", "", "dong y ok"},
    {CatPeople, "💁", "", "huong dan tipping hand"},
    {CatPeople, "🙋", "", "gio tay phat bieu raising hand"},
    {CatPeople, "🧏", "", "nghe"},
    {CatPeople, "🙇", "", "cui chao xin loi bow"},
    {CatPeople, "🤦", "", "vo tran facepalm"},
    {CatPeople, "🤷", "", "nhun vai shrug"},
    {CatPeople, "🧑‍⚕️", "", "bac si doctor"},
    {CatPeople, "🧑‍🎓", "", "sinh vien student"},
    {CatPeople, "🧑‍🏫", "", "giao vien teacher"},
    {CatPeople, "🧑‍💻", "", "lap trinh vien developer coder"},
    {CatPeople, "🧑‍🔬", "", "nha khoa hoc scientist"},
    {CatPeople, "🧑‍🎨", "", "hoa si artist"},
    {CatPeople, "🧑‍🚀", "", "phi hanh gia astronaut"},
    {CatPeople, "🧑‍🚒", "", "linh cuu hoa firefighter"},
    {CatPeople, "👮", "", "canh sat police"},
    {CatPeople, "🕵️", "", "tham tu detective"},
    {CatPeople, "👷", "", "cong nhan worker"},
    {CatPeople, "🤴", "", "hoang tu prince"},
    {CatPeople, "👸", "", "cong chua princess"},
    {CatPeople, "🎅", "", "ong gia noel santa"},
    {CatPeople, "🧙", "", "phap su mage"},
    {CatPeople, "🧛", "", "ma ca rong vampire"},
    {CatPeople, "🧟", "", "xac song zombie"},
    {CatPeople, "🏃", "", "chay bo runner"},
    {CatPeople, "🚶", "", "di bo walking"},
    {CatPeople, "💃", "", "nhay mua woman dancing"},
    {CatPeople, "🕺", "", "nhay mua man dancing"},
    {CatPeople, "🧘", "", "thien yoga lotus"},
    {CatPeople, "🛌", "", "ngu tren giuong sleeping bed"},
    {CatPeople, "🫂", "", "om nhau people hugging"},
    {CatPeople, "👪", "", "gia dinh family"},

    {CatAnimals, "🐶", "", "cho cun dog puppy"},
    {CatAnimals, "🐱", "", "meo cat"},
    {CatAnimals, "🐭", "", "chuot mouse"},
    {CatAnimals, "🐹", "", "chuot hamster"},
    {CatAnimals, "🐰", "", "tho rabbit"},
    {CatAnimals, "🦊", "", "cao fox"},
    {CatAnimals, "🐻", "", "gau bear"},
    {CatAnimals, "🐼", "", "gau truc panda"},
    {CatAnimals, "🐻‍❄️", "", "gau bac cuc polar bear"},
    {CatAnimals, "🐨", "", "gau tui koala"},
    {CatAnimals, "🐯", "", "ho cop tiger"},
    {CatAnimals, "🦁", "", "su tu lion"},
    {CatAnimals, "🐮", "", "bo sua cow"},
    {CatAnimals, "🐷", "", "lon heo pig"},
    {CatAnimals, "🐸", "", "ech frog"},
    {CatAnimals, "🐵", "", "khi monkey"},
    {CatAnimals, "🐔", "", "ga chicken"},
    {CatAnimals, "🐧", "", "chim canh cut penguin"},
    {CatAnimals, "🐦", "", "chim bird"},
    {CatAnimals, "🐤", "", "ga con chick"},
    {CatAnimals, "🦆", "", "vit duck"},
    {CatAnimals, "🦅", "", "dai bang eagle"},
    {CatAnimals, "🦉", "", "cu meo owl"},
    {CatAnimals, "🦇", "", "doi bat"},
    {CatAnimals, "🐺", "", "soi wolf"},
    {CatAnimals, "🐗", "", "lon rung boar"},
    {CatAnimals, "🐴", "", "ngua horse"},
    {CatAnimals, "🦄", "", "ky lan unicorn"},
    {CatAnimals, "🐝", "", "ong bee"},
    {CatAnimals, "🐛", "", "sau bug"},
    {CatAnimals, "🦋", "", "buom butterfly"},
    {CatAnimals, "🐌", "", "oc sen snail"},
    {CatAnimals, "🐞", "", "bo rua ladybug"},
    {CatAnimals, "🐜", "", "kien ant"},
    {CatAnimals, "🦟", "", "muoi mosquito"},
    {CatAnimals, "🐢", "", "rua turtle"},
    {CatAnimals, "🐍", "", "ran snake"},
    {CatAnimals, "🦎", "", "than lan lizard"},
    {CatAnimals, "🦖", "", "khung long t-rex"},
    {CatAnimals, "🦕", "", "khung long co dai"},
    {CatAnimals, "🐙", "", "bach tuoc octopus"},
    {CatAnimals, "🦑", "", "muc squid"},
    {CatAnimals, "🦐", "", "tom shrimp"},
    {CatAnimals, "🦀", "", "cua crab"},
    {CatAnimals, "🐡", "", "ca noc blowfish"},
    {CatAnimals, "🐠", "", "ca nhiet doi fish"},
    {CatAnimals, "🐟", "", "con ca fish"},
    {CatAnimals, "🐬", "", "ca heo dolphin"},
    {CatAnimals, "🐳", "", "ca voi whale"},
    {CatAnimals, "🦈", "", "ca map shark"},
    {CatAnimals, "🐊", "", "ca sau crocodile"},
    {CatAnimals, "🐘", "", "voi elephant"},
    {CatAnimals, "🦒", "", "huou cao co giraffe"},
    {CatAnimals, "🐪", "", "lac da camel"},
    {CatAnimals, "🦘", "", "chuot tui kangaroo"},
    {CatAnimals, "🐾", "", "dau chan thu paw prints"},
    {CatAnimals, "🐉", "", "rong dragon"},
    {CatAnimals, "💐", "", "bo hoa bouquet"},
    {CatAnimals, "🌸", "", "hoa anh dao cherry blossom"},
    {CatAnimals, "💮", "", "hoa trang white flower"},
    {CatAnimals, "🪷", "", "hoa sen lotus"},
    {CatAnimals, "🌹", "", "hoa hong rose"},
    {CatAnimals, "🥀", "", "hoa heo wilted flower"},
    {CatAnimals, "🌺", "", "hoa dam but hibiscus"},
    {CatAnimals, "🌻", "", "hoa huong duong sunflower"},
    {CatAnimals, "🌼", "", "hoa cuc blossom"},
    {CatAnimals, "🌷", "", "hoa tulip"},
    {CatAnimals, "🌱", "", "mam cay seedling"},
    {CatAnimals, "🌲", "", "cay thong evergreen"},
    {CatAnimals, "🌳", "", "cay xanh deciduous tree"},
    {CatAnimals, "🌴", "", "cay dua palm tree"},
    {CatAnimals, "🌵", "", "xuong rong cactus"},
    {CatAnimals, "🌾", "", "bong lua rice"},
    {CatAnimals, "🍀", "", "co bon la four leaf clover"},
    {CatAnimals, "🍁", "", "la phong maple leaf"},
    {CatAnimals, "🍂", "", "la rung fallen leaf"},
    {CatAnimals, "🍃", "", "la bay wind leaf"},
    {CatAnimals, "🍄", "", "nam mushroom"},
    {CatAnimals, "🌍", "", "trai dat earth globe"},
    {CatAnimals, "🌙", "", "mat trang moon"},
    {CatAnimals, "⭐", "", "ngoi sao star"},
    {CatAnimals, "🌟", "", "ngoi sao sang glowing star"},
    {CatAnimals, "✨", "", "lap lanh sparkles"},
    {CatAnimals, "☀️", "", "mat troi sun"},
    {CatAnimals, "⛅", "", "may che mat troi sun behind cloud"},
    {CatAnimals, "☁️", "", "dam may cloud"},
    {CatAnimals, "🌧️", "", "troi mua rain"},
    {CatAnimals, "⛈️", "", "mua bao thunder"},
    {CatAnimals, "⚡", "", "tia set lightning"},
    {CatAnimals, "❄️", "", "bong tuyet snowflake"},
    {CatAnimals, "🔥", "", "ngon lua fire hot"},
    {CatAnimals, "💧", "", "giot nuoc droplet"},
    {CatAnimals, "🌊", "", "song bien wave"},
    {CatAnimals, "🌈", "", "cau vong rainbow"},

    {CatFood, "🍇", "", "nho grapes"},
    {CatFood, "🍈", "", "dua luoi melon"},
    {CatFood, "🍉", "", "dua hau watermelon"},
    {CatFood, "🍊", "", "cam quyt orange"},
    {CatFood, "🍋", "", "chanh vang lemon"},
    {CatFood, "🍌", "", "chuoi banana"},
    {CatFood, "🍍", "", "dua thom pineapple"},
    {CatFood, "🥭", "", "xoai mango"},
    {CatFood, "🍎", "", "tao do red apple"},
    {CatFood, "🍏", "", "tao xanh green apple"},
    {CatFood, "🍐", "", "le pear"},
    {CatFood, "🍑", "", "dao peach"},
    {CatFood, "🍒", "", "anh dao cherries"},
    {CatFood, "🍓", "", "dau tay strawberry"},
    {CatFood, "🫐", "", "viet quat blueberries"},
    {CatFood, "🥝", "", "kiwi"},
    {CatFood, "🍅", "", "ca chua tomato"},
    {CatFood, "🥥", "", "dua coconut"},
    {CatFood, "🥑", "", "bo avocado"},
    {CatFood, "🍆", "", "ca tim eggplant"},
    {CatFood, "🥔", "", "khoai tay potato"},
    {CatFood, "🥕", "", "ca rot carrot"},
    {CatFood, "🌽", "", "bap ngo corn"},
    {CatFood, "🌶️", "", "ot cay hot pepper"},
    {CatFood, "🥒", "", "dua chuot cucumber"},
    {CatFood, "🥬", "", "rau xanh leafy green"},
    {CatFood, "🧄", "", "toi garlic"},
    {CatFood, "🧅", "", "hanh tay onion"},
    {CatFood, "🍞", "", "banh mi bread"},
    {CatFood, "🥐", "", "banh sung bo croissant"},
    {CatFood, "🥖", "", "banh mi dai baguette"},
    {CatFood, "🧀", "", "pho mai cheese"},
    {CatFood, "🥚", "", "trung egg"},
    {CatFood, "🍳", "", "trung op la cooking"},
    {CatFood, "🥞", "", "banh kep pancakes"},
    {CatFood, "🥩", "", "thit bit tet steak"},
    {CatFood, "🍗", "", "dui ga poultry leg"},
    {CatFood, "🍖", "", "thit meat"},
    {CatFood, "🌭", "", "xuc xich hotdog"},
    {CatFood, "🍔", "", "banh mi kep burger"},
    {CatFood, "🍟", "", "khoai tay chien fries"},
    {CatFood, "🍕", "", "banh pizza"},
    {CatFood, "🥪", "", "sandwich"},
    {CatFood, "🌮", "", "taco"},
    {CatFood, "🍜", "", "mi pho bun ramen noodles"},
    {CatFood, "🍝", "", "mi y spaghetti"},
    {CatFood, "🍚", "", "com trang rice"},
    {CatFood, "🍛", "", "com ca ri curry"},
    {CatFood, "🍣", "", "sushi"},
    {CatFood, "🍱", "", "hop com bento"},
    {CatFood, "🥟", "", "banh bao ha cao dumpling"},
    {CatFood, "🍤", "", "tom chien fried shrimp"},
    {CatFood, "🍦", "", "kem oc que ice cream"},
    {CatFood, "🍧", "", "da bao shaved ice"},
    {CatFood, "🍩", "", "banh vong donut"},
    {CatFood, "🍪", "", "banh quy cookie"},
    {CatFood, "🎂", "", "banh sinh nhat birthday cake"},
    {CatFood, "🍰", "", "banh ngot shortcake"},
    {CatFood, "🧁", "", "cupcake"},
    {CatFood, "🍫", "", "so co la chocolate"},
    {CatFood, "🍬", "", "keo candy"},
    {CatFood, "🍭", "", "keo mut lollipop"},
    {CatFood, "🍮", "", "banh flan custard"},
    {CatFood, "🍯", "", "mat ong honey"},
    {CatFood, "🍼", "", "binh sua baby bottle"},
    {CatFood, "🥛", "", "ly sua milk"},
    {CatFood, "☕", "", "ca phe coffee"},
    {CatFood, "🍵", "", "tra xanh tea"},
    {CatFood, "🧋", "", "tra sua bubble tea boba"},
    {CatFood, "🧃", "", "nuoc ep hop juice"},
    {CatFood, "🥤", "", "nuoc ngot cup with straw"},
    {CatFood, "🍺", "", "coc bia beer"},
    {CatFood, "🍻", "", "nang ly bia cheers"},
    {CatFood, "🥂", "", "chuc mung champagne"},
    {CatFood, "🍷", "", "ruou vang wine"},
    {CatFood, "🥃", "", "ruou manh whisky"},
    {CatFood, "🍸", "", "cocktail"},
    {CatFood, "🍾", "", "sam banh bottle"},
    {CatFood, "🍽️", "", "dia nia dao fork knife plate"},

    {CatTravel, "🚗", "", "o to xe hoi car"},
    {CatTravel, "🚕", "", "xe taxi"},
    {CatTravel, "🚙", "", "xe suv"},
    {CatTravel, "🚌", "", "xe buyt bus"},
    {CatTravel, "🚎", "", "xe dien trolleybus"},
    {CatTravel, "🏎️", "", "xe dua racing car"},
    {CatTravel, "🚓", "", "xe canh sat police car"},
    {CatTravel, "🚑", "", "xe cuu thuong ambulance"},
    {CatTravel, "🚒", "", "xe cuu hoa fire engine"},
    {CatTravel, "🚚", "", "xe tai truck"},
    {CatTravel, "🚜", "", "may cay tractor"},
    {CatTravel, "🏍️", "", "xe may mo to motorcycle"},
    {CatTravel, "🛵", "", "xe tay ga scooter"},
    {CatTravel, "🚲", "", "xe dap bicycle"},
    {CatTravel, "🛴", "", "xe truot kick scooter"},
    {CatTravel, "🚂", "", "tau hoa locomotive"},
    {CatTravel, "🚆", "", "tau dien ngam train"},
    {CatTravel, "🚄", "", "tau cao toc high speed train"},
    {CatTravel, "✈️", "", "may bay airplane"},
    {CatTravel, "🛫", "", "cat canh departure"},
    {CatTravel, "🛬", "", "ha canh arrival"},
    {CatTravel, "🚁", "", "truc thang helicopter"},
    {CatTravel, "🚀", "", "ten lua rocket"},
    {CatTravel, "🛸", "", "dia bay ufo"},
    {CatTravel, "⛵", "", "thuyen buom sailboat"},
    {CatTravel, "🚤", "", "ca no speedboat"},
    {CatTravel, "🚢", "", "tau thuy ship"},
    {CatTravel, "⚓", "", "mo neo anchor"},
    {CatTravel, "⛽", "", "cay xang fuel pump"},
    {CatTravel, "🚦", "", "den giao thong traffic light"},
    {CatTravel, "🗺️", "", "ban do world map"},
    {CatTravel, "🧭", "", "la ban compass"},
    {CatTravel, "🏔️", "", "nui tuyet snow mountain"},
    {CatTravel, "⛰️", "", "ngon nui mountain"},
    {CatTravel, "🌋", "", "nui lua volcano"},
    {CatTravel, "🏕️", "", "cam trai camping"},
    {CatTravel, "🏖️", "", "bai bien beach"},
    {CatTravel, "🏜️", "", "sa mac desert"},
    {CatTravel, "🏝️", "", "dao hoang island"},
    {CatTravel, "🏟️", "", "san van dong stadium"},
    {CatTravel, "🏛️", "", "toa nha co dien"},
    {CatTravel, "🏠", "", "ngoi nha house"},
    {CatTravel, "🏡", "", "nha vuon home"},
    {CatTravel, "🏢", "", "toa nha van phong office"},
    {CatTravel, "🏥", "", "benh vien hospital"},
    {CatTravel, "🏦", "", "ngan hang bank"},
    {CatTravel, "🏨", "", "khach san hotel"},
    {CatTravel, "🏫", "", "truong hoc school"},
    {CatTravel, "🗼", "", "thap tokyo tower"},
    {CatTravel, "🗽", "", "tuong nu than tu do"},
    {CatTravel, "⛪", "", "nha tho church"},
    {CatTravel, "⛩️", "", "cong torii shinto"},
    {CatTravel, "🌅", "", "binh minh sunrise"},
    {CatTravel, "🌄", "", "hoang hon"},
    {CatTravel, "🌆", "", "thanh pho chieu"},
    {CatTravel, "🌇", "", "mat troi lan sunset"},
    {CatTravel, "🌃", "", "thanh pho dem night stars"},
    {CatTravel, "🌉", "", "cau dem bridge night"},
    {CatTravel, "🎆", "", "phao hoa fireworks"},
    {CatTravel, "🎇", "", "phao sang sparkler"},

    {CatActivities, "⚽", "", "bong da soccer football"},
    {CatActivities, "🏀", "", "bong ro basketball"},
    {CatActivities, "🏈", "", "bong bau duc"},
    {CatActivities, "⚾", "", "bong chay baseball"},
    {CatActivities, "🎾", "", "quan vot tennis"},
    {CatActivities, "🏐", "", "bong chuyen volleyball"},
    {CatActivities, "🏉", "", "rugby"},
    {CatActivities, "🎱", "", "bi a billiards 8ball"},
    {CatActivities, "🏓", "", "bong ban ping pong"},
    {CatActivities, "🏸", "", "cau long badminton"},
    {CatActivities, "🥊", "", "quyen anh boxing glove"},
    {CatActivities, "🥋", "", "vo thuat martial arts"},
    {CatActivities, "⛳", "", "golf"},
    {CatActivities, "🏹", "", "ban cung archery"},
    {CatActivities, "🎣", "", "cau ca fishing"},
    {CatActivities, "🤿", "", "lan bien diving"},
    {CatActivities, "🎿", "", "truot tuyet ski"},
    {CatActivities, "🏆", "", "cup vo dich trophy"},
    {CatActivities, "🥇", "", "huy chuong vang gold medal"},
    {CatActivities, "🥈", "", "huy chuong bac silver medal"},
    {CatActivities, "🥉", "", "huy chuong dong bronze medal"},
    {CatActivities, "🏅", "", "huy chuong sports medal"},
    {CatActivities, "🎖️", "", "huan chuong military medal"},
    {CatActivities, "🎫", "", "ve xem ticket"},
    {CatActivities, "🎭", "", "san khau kich performing arts"},
    {CatActivities, "🎨", "", "ve tranh art palette"},
    {CatActivities, "🎬", "", "phim anh clapper board"},
    {CatActivities, "🎤", "", "hat karaoke microphone"},
    {CatActivities, "🎧", "", "tai nghe nhac headphones"},
    {CatActivities, "🎼", "", "not nhac musical score"},
    {CatActivities, "🎹", "", "dan piano"},
    {CatActivities, "🥁", "", "trong drum"},
    {CatActivities, "🎷", "", "ken saxophone"},
    {CatActivities, "🎸", "", "dan guitar"},
    {CatActivities, "🎻", "", "dan violin"},
    {CatActivities, "🎲", "", "xuc xac dice"},
    {CatActivities, "♟️", "", "co vua chess"},
    {CatActivities, "🎯", "", "muc tieu bullseye dart"},
    {CatActivities, "🎳", "", "bowling"},
    {CatActivities, "🎮", "", "choi game video controller"},
    {CatActivities, "🕹️", "", "tay cam joystick"},
    {CatActivities, "🧩", "", "ghep hinh puzzle"},
    {CatActivities, "🎉", "", "chuc mung party popper"},
    {CatActivities, "🎊", "", "phao giay confetti ball"},
    {CatActivities, "🎈", "", "bong bay balloon"},
    {CatActivities, "🎁", "", "hop qua gift present"},
    {CatActivities, "🎀", "", "no hong ribbon"},
    {CatActivities, "🎄", "", "cay thong noel christmas"},
    {CatActivities, "🎃", "", "bi ngo halloween"},
    {CatActivities, "🧧", "", "bao li xi red envelope"},

    {CatObjects, "⌚", "", "dong ho deo tay watch"},
    {CatObjects, "📱", "", "dien thoai smartphone phone"},
    {CatObjects, "💻", "", "may tinh laptop"},
    {CatObjects, "🖥️", "", "man hinh may tinh desktop"},
    {CatObjects, "⌨️", "", "ban phim keyboard"},
    {CatObjects, "🖱️", "", "chuot may tinh mouse"},
    {CatObjects, "🖨️", "", "may in printer"},
    {CatObjects, "📷", "", "may anh camera"},
    {CatObjects, "📹", "", "may quay video camera"},
    {CatObjects, "🎥", "", "may quay phim movie"},
    {CatObjects, "📞", "", "dien thoai ban telephone"},
    {CatObjects, "📺", "", "tivi television"},
    {CatObjects, "📻", "", "dai radio"},
    {CatObjects, "⏰", "", "dong ho bao thuc alarm clock"},
    {CatObjects, "⏳", "", "dong ho cat hourglass"},
    {CatObjects, "🔋", "", "pin battery"},
    {CatObjects, "🔌", "", "phich cam electric plug"},
    {CatObjects, "💡", "", "bong den y tuong light bulb idea"},
    {CatObjects, "🔦", "", "den pin flashlight"},
    {CatObjects, "🕯️", "", "ngon nen candle"},
    {CatObjects, "💸", "", "tien bay money with wings"},
    {CatObjects, "💵", "", "tien do la dollar"},
    {CatObjects, "💰", "", "tui tien money bag"},
    {CatObjects, "💳", "", "the tin dung credit card"},
    {CatObjects, "💎", "", "kim cuong gem diamond"},
    {CatObjects, "⚖️", "", "can cong ly balance scale"},
    {CatObjects, "🔧", "", "co le wrench"},
    {CatObjects, "🔨", "", "cai bua hammer"},
    {CatObjects, "🛠️", "", "cong cu tools"},
    {CatObjects, "⚙️", "", "banh rang cai dat gear settings"},
    {CatObjects, "🧲", "", "nam cham magnet"},
    {CatObjects, "🔫", "", "sung nuoc water pistol"},
    {CatObjects, "💣", "", "qua bom bomb"},
    {CatObjects, "🔪", "", "con dao knife"},
    {CatObjects, "🛡️", "", "khien bao ve shield"},
    {CatObjects, "🔑", "", "chia khoa key"},
    {CatObjects, "🗝️", "", "chia khoa co old key"},
    {CatObjects, "🔒", "", "khoa lock"},
    {CatObjects, "🔓", "", "mo khoa unlock"},
    {CatObjects, "🔔", "", "chuong thong bao bell"},
    {CatObjects, "🔕", "", "tat chuong muted bell"},
    {CatObjects, "📣", "", "loa thong bao megaphone"},
    {CatObjects, "📢", "", "loa loudspeaker"},
    {CatObjects, "📚", "", "sach vo books"},
    {CatObjects, "📖", "", "quyen sach mo open book"},
    {CatObjects, "📝", "", "ghi chu memo note"},
    {CatObjects, "✏️", "", "but chi pencil"},
    {CatObjects, "🖊️", "", "but bi pen"},
    {CatObjects, "📁", "", "thu muc folder"},
    {CatObjects, "📂", "", "mo thu muc open folder"},
    {CatObjects, "📅", "", "lich calendar"},
    {CatObjects, "📌", "", "ghim pushpin"},
    {CatObjects, "📎", "", "kep giay paperclip"},
    {CatObjects, "✂️", "", "cai keo scissors"},
    {CatObjects, "🗑️", "", "thung rac trash"},
    {CatObjects, "✉️", "", "thu email envelope"},
    {CatObjects, "📩", "", "hop thu den"},
    {CatObjects, "📦", "", "goi hang package box"},
    {CatObjects, "🏷️", "", "nhan label tag"},
    {CatObjects, "💼", "", "cap cong so briefcase work"},
    {CatObjects, "📊", "", "bieu do thong ke chart"},
    {CatObjects, "📈", "", "tang truong chart increasing"},
    {CatObjects, "📉", "", "suy giam chart decreasing"},
    {CatObjects, "🔍", "", "kinh lup tim kiem search"},
    {CatObjects, "💊", "", "vien thuoc pill"},
    {CatObjects, "🩺", "", "ong nghe bac si stethoscope"},
    {CatObjects, "🛒", "", "xe day sieu thi shopping cart"},
    {CatObjects, "🛍️", "", "tui mua sam shopping bags"},

    {CatSymbols, "❤️", "", "trai tim do red heart love"},
    {CatSymbols, "🧡", "", "trai tim cam orange heart"},
    {CatSymbols, "💛", "", "trai tim vang yellow heart"},
    {CatSymbols, "💚", "", "trai tim xanh la green heart"},
    {CatSymbols, "🩵", "", "trai tim xanh nhat light blue heart"},
    {CatSymbols, "💙", "", "trai tim xanh duong blue heart"},
    {CatSymbols, "💜", "", "trai tim tim purple heart"},
    {CatSymbols, "🤎", "", "trai tim nau brown heart"},
    {CatSymbols, "🖤", "", "trai tim den black heart"},
    {CatSymbols, "🩶", "", "trai tim xam grey heart"},
    {CatSymbols, "🤍", "", "trai tim trang white heart"},
    {CatSymbols, "🩷", "", "trai tim hong pink heart"},
    {CatSymbols, "✅", "", "hoan thanh check mark ok"},
    {CatSymbols, "☑️", "", "dau tich checkbox"},
    {CatSymbols, "✔️", "", "dung check"},
    {CatSymbols, "❌", "", "sai xoa cross mark"},
    {CatSymbols, "❎", "", "tu choi cross"},
    {CatSymbols, "➕", "", "dau cong plus"},
    {CatSymbols, "➖", "", "dau tru minus"},
    {CatSymbols, "➗", "", "dau chia divide"},
    {CatSymbols, "✖️", "", "dau nhan multiply"},
    {CatSymbols, "❓", "", "hoi cham question"},
    {CatSymbols, "❔", "", "hoi cham trang"},
    {CatSymbols, "❗", "", "cham than exclamation"},
    {CatSymbols, "❕", "", "cham than trang"},
    {CatSymbols, "‼️", "", "hai cham than"},
    {CatSymbols, "⁉️", "", "cham than hoi"},
    {CatSymbols, "⚠️", "", "canh bao warning"},
    {CatSymbols, "🚫", "", "cam prohibited"},
    {CatSymbols, "⛔", "", "cam vao no entry"},
    {CatSymbols, "🔞", "", "18 cong no under eighteen"},
    {CatSymbols, "📵", "", "cam dien thoai"},
    {CatSymbols, "🔇", "", "tat tieng muted"},
    {CatSymbols, "🔊", "", "bat loa loud sound"},
    {CatSymbols, "♻️", "", "tai che recycle"},
    {CatSymbols, "⚜️", "", "bieu tuong"},
    {CatSymbols, "🔱", "", "dinh ba trident"},
    {CatSymbols, "📛", "", "the ten name badge"},
    {CatSymbols, "⭕", "", "vong tron do"},
    {CatSymbols, "🛑", "", "bien dung stop sign"},
    {CatSymbols, "🚩", "", "co do red flag"},
    {CatSymbols, "🔔", "", "chuong"},
    {CatSymbols, "🎵", "", "not nhac musical note"},
    {CatSymbols, "🎶", "", "nhieu not nhac notes"},
    {CatSymbols, "🔴", "", "tron do red circle"},
    {CatSymbols, "🟠", "", "tron cam orange circle"},
    {CatSymbols, "🟡", "", "tron vang yellow circle"},
    {CatSymbols, "🟢", "", "tron xanh la green circle"},
    {CatSymbols, "🔵", "", "tron xanh duong blue circle"},
    {CatSymbols, "🟣", "", "tron tim purple circle"},
    {CatSymbols, "⚫", "", "tron den black circle"},
    {CatSymbols, "⚪", "", "tron trang white circle"},
    {CatSymbols, "🔺", "", "tam giac do"},
    {CatSymbols, "🔸", "", "hinh thoi cam"},
    {CatSymbols, "🔹", "", "hinh thoi xanh"},
    {CatSymbols, "🆕", "", "moi new"},
    {CatSymbols, "🆗", "", "ok"},
    {CatSymbols, "🆙", "", "up"},
    {CatSymbols, "🆒", "", "cool"},
    {CatSymbols, "🆓", "", "mien phi free"},
    {CatSymbols, "🆘", "", "sos khan cap"},
    {CatSymbols, "ℹ️", "", "thong tin info"},
    {CatSymbols, "🔄", "", "tai lai refresh"},
    {CatSymbols, "▶️", "", "phat play"},
    {CatSymbols, "⏸️", "", "tam dung pause"},
    {CatSymbols, "⏹️", "", "dung stop"},
    {CatSymbols, "➡️", "", "mui ten phai arrow right"},
    {CatSymbols, "⬅️", "", "mui ten trai arrow left"},
    {CatSymbols, "⬆️", "", "mui ten len arrow up"},
    {CatSymbols, "⬇️", "", "mui ten xuong arrow down"},
    {CatSymbols, "©️", "", "ban quyen copyright"},
    {CatSymbols, "®️", "", "thuong hieu registered"},
    {CatSymbols, "™️", "", "trademark"},

    {CatFlags, "🇻🇳", "", "viet nam vietnam vn"},
    {CatFlags, "🇺🇸", "", "my hoa ky usa america"},
    {CatFlags, "🇬🇧", "", "anh uk britain"},
    {CatFlags, "🇯🇵", "", "nhat ban japan jp"},
    {CatFlags, "🇰🇷", "", "han quoc korea kr"},
    {CatFlags, "🇨🇳", "", "trung quoc china cn"},
    {CatFlags, "🇹🇼", "", "dai loan taiwan"},
    {CatFlags, "🇸🇬", "", "singapore sg"},
    {CatFlags, "🇹🇭", "", "thai lan thailand"},
    {CatFlags, "🇲🇾", "", "malaysia"},
    {CatFlags, "🇮🇩", "", "indonesia"},
    {CatFlags, "🇵🇭", "", "philippines"},
    {CatFlags, "🇱🇦", "", "lao laos"},
    {CatFlags, "🇰🇭", "", "campuchia cambodia"},
    {CatFlags, "🇮🇳", "", "an do india"},
    {CatFlags, "🇦🇺", "", "uc australia"},
    {CatFlags, "🇨🇦", "", "canada"},
    {CatFlags, "🇫🇷", "", "phap france"},
    {CatFlags, "🇩🇪", "", "duc germany"},
    {CatFlags, "🇮🇹", "", "y italy"},
    {CatFlags, "🇪🇸", "", "tay ban nha spain"},
    {CatFlags, "🇷🇺", "", "nga russia"},
    {CatFlags, "🇧🇷", "", "brazil"},
    {CatFlags, "🇳🇱", "", "ha lan netherlands"},
    {CatFlags, "🇨🇭", "", "thuy si switzerland"},
    {CatFlags, "🇸🇪", "", "thuy dien sweden"},
    {CatFlags, "🇳🇿", "", "new zealand"},
    {CatFlags, "🇦🇪", "", "uae dubai"},
    {CatFlags, "🇸🇦", "", "saudi arabia"},
    {CatFlags, "🏳️", "", "co trang white flag"},
    {CatFlags, "🏴", "", "co den black flag"},
    {CatFlags, "🏁", "", "co dua checkered flag"},
    {CatFlags, "🏳️‍🌈", "", "co cau vong rainbow flag"},
};

QString stripAccents(const QString& input) {
    const QString norm = input.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(norm.size());
    for (const QChar ch : norm) {
        if (ch.unicode() == 0x0111 || ch.unicode() == 0x0110) {
            out.append(QLatin1Char('d'));
            continue;
        }
        if (ch.category() != QChar::Mark_NonSpacing && ch.category() != QChar::Mark_SpacingCombining)
            out.append(ch.toLower());
    }
    return out;
}

int findCategoryPos(const QString& id) {
    const int n = static_cast<int>(sizeof(kCategories) / sizeof(kCategories[0]));
    for (int i = 0; i < n; ++i) {
        if (id == QLatin1String(kCategories[i].id)) return i;
    }
    return 1;
}

QVariantMap makeItem(const EmojiDef& e) {
    return QVariantMap{
        {QStringLiteral("glyph"), QString::fromUtf8(e.glyph)},
        {QStringLiteral("code"), QString::fromLatin1(e.code)},
        {QStringLiteral("keywords"), QString::fromLatin1(e.keywords)},
    };
}

const QVector<QVariantList>& categoryItemsCache() {
    static const QVector<QVariantList> cache = []() {
        const int catCount = static_cast<int>(sizeof(kCategories) / sizeof(kCategories[0]));
        QVector<QVariantList> buckets(qMax(0, catCount - 1));
        for (const EmojiDef& e : kEmojis) {
            const int idx = static_cast<int>(e.cat);
            if (idx >= 0 && idx < buckets.size()) buckets[idx].append(makeItem(e));
        }
        return buckets;
    }();
    return cache;
}

QString findCodeForGlyph(const QString& glyph) {
    for (const EmojiDef& e : kEmojis) {
        if (e.code[0] != '\0' && QString::fromUtf8(e.glyph) == glyph)
            return QString::fromLatin1(e.code);
    }
    return QString();
}

QVariantList buildRecentItems(const QStringList& recent) {
    QVariantList out;
    out.reserve(recent.size());
    for (const QString& g : recent) {
        out.append(QVariantMap{
            {QStringLiteral("glyph"), g},
            {QStringLiteral("code"), findCodeForGlyph(g)},
            {QStringLiteral("keywords"), QString()},
        });
    }
    return out;
}

QVariantList buildSearchItems(const QString& rawQuery) {
    QVariantList out;
    const QString trimmed = rawQuery.trimmed();
    const QString q = stripAccents(trimmed);
    if (q.isEmpty()) return out;
    for (const EmojiDef& e : kEmojis) {
        if (out.size() >= EmojiCatalog::kMaxSearch) break;
        const QString code = QString::fromLatin1(e.code).toLower();
        const QString kw = QString::fromLatin1(e.keywords);
        if (code.contains(q) || kw.contains(q) || QString::fromUtf8(e.glyph) == trimmed)
            out.append(makeItem(e));
    }
    return out;
}

QVariantMap makeSection(const char* id, const char* nameVi, const char* nameEn, const QVariantList& items) {
    return QVariantMap{
        {QStringLiteral("id"), QString::fromLatin1(id)},
        {QStringLiteral("nameVi"), QString::fromUtf8(nameVi)},
        {QStringLiteral("nameEn"), QString::fromUtf8(nameEn)},
        {QStringLiteral("items"), items},
    };
}

}

EmojiCatalog::EmojiCatalog(QObject* parent) : QObject(parent) {}

int EmojiCatalog::totalCount() {
    return static_cast<int>(sizeof(kEmojis) / sizeof(kEmojis[0]));
}

int EmojiCatalog::categoryCount() {
    return static_cast<int>(sizeof(kCategories) / sizeof(kCategories[0]));
}

QVariantList EmojiCatalog::categories() const {
    QVariantList out;
    out.reserve(categoryCount());
    for (const CategoryDef& c : kCategories) {
        out.append(QVariantMap{
            {QStringLiteral("id"), QString::fromLatin1(c.id)},
            {QStringLiteral("glyph"), QString::fromUtf8(c.glyph)},
            {QStringLiteral("nameVi"), QString::fromUtf8(c.nameVi)},
            {QStringLiteral("nameEn"), QString::fromUtf8(c.nameEn)},
        });
    }
    return out;
}

QString EmojiCatalog::currentCategoryNameVi() const {
    return QString::fromUtf8(kCategories[findCategoryPos(m_currentCategory)].nameVi);
}

QString EmojiCatalog::currentCategoryNameEn() const {
    return QString::fromUtf8(kCategories[findCategoryPos(m_currentCategory)].nameEn);
}

void EmojiCatalog::ensureRecentLoaded() const {
    if (m_recentLoaded) return;
    m_recentLoaded = true;
    m_recent = QSettings()
                   .value(QStringLiteral("emoji/recent"))
                   .toStringList();
    if (m_recent.size() > kMaxRecent) m_recent = m_recent.mid(0, kMaxRecent);
}

void EmojiCatalog::saveRecent() const {
    QSettings settings;
    settings.setValue(QStringLiteral("emoji/recent"), m_recent);
    settings.sync();
}

QVariantList EmojiCatalog::items() const {
    if (!m_searchQuery.trimmed().isEmpty()) return buildSearchItems(m_searchQuery);

    if (m_currentCategory == QLatin1String("recent")) {
        ensureRecentLoaded();
        return buildRecentItems(m_recent);
    }

    const int pos = findCategoryPos(m_currentCategory);
    if (pos <= 0) return {};
    return categoryItemsCache().value(pos - 1);
}

QVariantList EmojiCatalog::sections() const {
    QVariantList out;
    if (!m_searchQuery.trimmed().isEmpty()) {
        const QVariantList matched = buildSearchItems(m_searchQuery);
        if (!matched.isEmpty())
            out.append(makeSection("search", "Kết quả tìm kiếm", "Search results", matched));
        return out;
    }

    ensureRecentLoaded();
    if (!m_recent.isEmpty())
        out.append(makeSection(kCategories[0].id, kCategories[0].nameVi, kCategories[0].nameEn, buildRecentItems(m_recent)));

    const auto& cached = categoryItemsCache();
    const int catCount = categoryCount();
    for (int i = 1; i < catCount; ++i) {
        const QVariantList& catItems = cached.value(i - 1);
        if (!catItems.isEmpty())
            out.append(makeSection(kCategories[i].id, kCategories[i].nameVi, kCategories[i].nameEn, catItems));
    }
    return out;
}

QVariantList EmojiCatalog::flatRows(int columns, int revision) const {
    Q_UNUSED(revision);
    const int cols = qMax(1, columns);
    QVariantList out;
    const QVariantList secs = sections();
    for (const QVariant& sv : secs) {
        const QVariantMap sec = sv.toMap();
        const QVariantList items = sec.value(QStringLiteral("items")).toList();
        if (items.isEmpty()) continue;
        out.append(QVariantMap{
            {QStringLiteral("isHeader"), true},
            {QStringLiteral("nameVi"), sec.value(QStringLiteral("nameVi"))},
            {QStringLiteral("nameEn"), sec.value(QStringLiteral("nameEn"))},
        });
        for (int i = 0; i < items.size(); i += cols) {
            out.append(QVariantMap{
                {QStringLiteral("isHeader"), false},
                {QStringLiteral("items"), items.mid(i, cols)},
            });
        }
    }
    return out;
}

void EmojiCatalog::selectCategory(const QString& categoryId) {
    const QString next = QString::fromLatin1(kCategories[findCategoryPos(categoryId)].id);
    if (m_currentCategory == next && m_searchQuery.isEmpty()) return;
    m_currentCategory = next;
    m_searchQuery.clear();
    ++m_revision;
    emit itemsChanged();
}

void EmojiCatalog::search(const QString& query) {
    if (m_searchQuery == query) return;
    m_searchQuery = query;
    ++m_revision;
    emit itemsChanged();
}

void EmojiCatalog::recordRecent(const QString& glyph) {
    const QString g = glyph.trimmed();
    if (g.isEmpty()) return;
    ensureRecentLoaded();
    m_recent.removeAll(g);
    m_recent.prepend(g);
    if (m_recent.size() > kMaxRecent) m_recent = m_recent.mid(0, kMaxRecent);
    saveRecent();
    ++m_revision;
    emit itemsChanged();
}

}
