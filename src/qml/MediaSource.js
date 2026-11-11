.pragma library

function toSource(url) {
    if (!url) return "";
    const s = String(url);
    if (s.indexOf("http://") === 0 || s.indexOf("https://") === 0)
        return "image://media/" + encodeURIComponent(s);
    return s;
}
