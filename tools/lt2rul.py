#!/usr/bin/env python3
"""
lt2rul.py -- LanguageTool grammar.xml  ->  gramcheck .rul

Runs on the host (Linux/Mac/Windows), never on the target. Reads a
LanguageTool language pack's grammar.xml and emits the subset of rules
that map cleanly onto gramcheck's compact runtime

What we DO extract:

  * <rule ...> blocks whose <pattern> is 1 or 2 plain literal <token>
    elements followed by a <suggestion>literal</suggestion>, i.e. simple
    word confusions, missing diacritics and common 2-word phrases ->
    gramcheck PAIR entries.

  * <rule name="..."> with obvious punctuation-position patterns
    detected heuristically from tag id/name (uses id like
    "COMMA_WHITESPACE", "WHITESPACE_BEFORE_PUNCTUATION",
    "DOUBLE_PUNCTUATION" etc.). We do NOT try to run LT's regex
    engine; we just enable the corresponding gramcheck rule

What we SKIP:

  * multi-token pattern rules
  * anything using postag=, chunk=, or exception blocks
  * disambiguator rules
  * everything with <phrases> refs

Usage:
    python3 lt2rul.py <path/to/grammar.xml> [<path/to/style.xml> ...] <lang_code>  > <lang>.rul

Example:
    python3 lt2rul.py \
        LanguageTool-6.0/org/languagetool/rules/en/grammar.xml \
        LanguageTool-6.0/org/languagetool/rules/en/style.xml en \
        > en.rul

python3 lt2rul.py grammar.xml style.xml es > es.rul

Note on licensing:
    LanguageTool rules are LGPL. The .rul files produced from them
    inherit that license. Ship them alongside the LGPL notice. Do NOT
    mix them into the GPL-2+ source tree of gramcheck; keep them as
    data files
"""

from __future__ import annotations

import sys
import re
import argparse
import os
try:
    from lxml import etree as ET
    LXML_AVAILABLE = True
except ImportError:
    import xml.etree.ElementTree as ET
    LXML_AVAILABLE = False


# ------------------------------------------------------------------
# Manual word lists per language (LanguageTool does not provide these
# consistently in all languages, so we ship them as defaults)
# ------------------------------------------------------------------

# ------------------------------------------------------------------
# Manual PAIR fallback per language (covers confusions that LanguageTool
# does not expose as simple literal->literal rules in every pack)
# ------------------------------------------------------------------

MANUAL_PAIRS = {
    "en": [
        ("teh", "the"),
        ("recieve", "receive"),
        ("seperate", "separate"),
        ("definately", "definitely"),
        ("occured", "occurred"),
        ("accomodate", "accommodate"),
        ("begining", "beginning"),
        ("neccessary", "necessary"),
        ("wich", "which"),
        ("alot", "a lot"),
        ("its", "it's"),
        ("your", "you're"),
        ("there", "their"),
        ("affect", "effect"),
        ("loose", "lose"),
        ("then", "than"),
        ("should of", "should have"),
        ("could of", "could have"),
        ("would of", "would have"),
    ],
    "es": [
        ("aver", "a ver"),
        ("aya", "haya"),
        ("halla", "haya"),
        ("echo", "hecho"),
        ("porque", "por qué"),
        ("porqué", "por qué"),
        ("sino", "si no"),
        ("tambien", "también"),
        ("esta", "está"),
        ("mas", "más"),
        ("tu", "tú"),
        ("el", "él"),
        ("si", "sí"),
        ("te", "té"),
        ("se", "sé"),
        ("osea", "o sea"),
        ("ademas", "además"),
        ("ingles", "inglés"),
        ("frances", "francés"),
        ("aleman", "alemán"),
        ("espanol", "español"),
        ("ninos", "niños"),
        ("anos", "años"),
        ("haber", "a ver"),
        ("valla", "vaya"),
        ("vaya", "valla"),
        ("tubo", "tuvo"),
        ("tuvo", "tubo"),
        ("hechar", "echar"),
        ("haiga", "haya"),
        ("nesecito", "necesito"),
        ("dijistes", "dijiste"),
        ("vinistes", "viniste"),
    ],
    "fr": [
        ("parceque", "parce que"),
        ("cad", "c'est-à-dire"),
        ("ca", "ça"),
        ("a", "à"),
        ("ou", "où"),
        ("sa", "ça"),
        ("ces", "ses"),
        ("se", "ce"),
        ("malgres", "malgré"),
        ("language", "langage"),
    ],
    "de": [
        ("standart", "Standard"),
        ("seit", "seit / seid"),
        ("seid", "seit / seid"),
        ("das", "dass"),
        ("wieder", "wider"),
        ("wird", "wirt"),
        ("tod", "tot"),
        ("endgültig", "endgültig"),
    ],
    "it": [
        ("qual'è", "qual è"),
        ("po", "po'"),
        ("se", "sé"),
        ("da", "dà"),
        ("ne", "né"),
        ("gli", "li"),
        ("sopratutto", "soprattutto"),
        ("daccordo", "d'accordo"),
        ("perchè", "perché"),
    ],
    "pt": [
        ("a", "à"),
        ("as", "às"),
        ("ha", "há"),
        ("mais", "mas"),
        ("porque", "porquê"),
        ("onde", "aonde"),
        ("sinao", "senão"),
        ("concerteza", "com certeza"),
        ("derrepente", "de repente"),
    ],
}


MANUAL_WORDLISTS = {
    "en": {
        "PUNCT_NO_SPACE_BEFORE": ",.;:!?)",
        "PUNCT_SPACE_AFTER": ",;:",
        "PUNCT_NO_SPACE_AFTER": "(",
        "PUNCT_NO_DOUBLE": ",;",
        "EXCESSIVE_PUNCT": "2",
        "WORD_CAP": "monday,tuesday,wednesday,thursday,friday,saturday,sunday,january,february,march,april,june,july,august,september,october,november,december,english,american,british,european,spanish,french,german,london,paris,europe,america",
        "WORD_UPPER": "usa,uk,eu,un,nato,gdp,vat,dna,usb,pdf,html,url,isp,bbs,faq,ceo,ftn",
        "ALL_CAPS_WORD": "5",
        "REPEAT_WORD_SKIP": "had,that,ha,ho,so,no,is,ok",
        "BRACKET_PAIRS": "()[]{}",
        "SENTENCE_TOO_LONG": "30",
        "TOO_MANY_COMMAS": "4 10",
        "WORD_DENSITY": "3 12",
    },
    "es": {
        "PUNCT_NO_SPACE_BEFORE": ",.;:!?)",
        "PUNCT_SPACE_AFTER": ",;:",
        "PUNCT_NO_SPACE_AFTER": "(¿¡",
        "PUNCT_NO_DOUBLE": ",;",
        "EXCESSIVE_PUNCT": "2",
        "SENTENCE_OPEN_ES": "?!",
        "WORD_CAP": "España,Europa,Madrid,Barcelona,Sevilla,Valencia,América,África,Asia,Alemania,Francia,Italia,Portugal,Canarias,Tenerife",
        "WORD_UPPER": "UE,ONU,OTAN,PIB,IVA,DNI,ADN,USB,PDF,HTML,URL,ISP,BBS,FTN,RAE",
        "ALL_CAPS_WORD": "6",
        "REPEAT_WORD_SKIP": "de,que,y,o,a,en,ni,muy,ja,je,ha",
        "BRACKET_PAIRS": "()[]{}«»",
        "ARTICLE_FEM": "la,una,las",
        "ARTICLE_MASC": "el,un,los,unos",
        "FEM_ENDINGS": "a,as",
        "MASC_ENDINGS": "o,os",
        "MASC_EXCEPTION_ENDINGS": "ma",
        "AGREEMENT_EXCEPTIONS": "agua,alma,área,aula,hacha,águila,arma,ala,hambre,ave,mano,foto,moto,radio,día,mapa,sofá,planeta,poeta,pirata,cura,guía,turista,artista,dentista,periodista,futbolista,taxista,modelo,testigo,soprano,grande,verde,triste,alegre,fuerte,inteligente,interesante,importante,posible,amable,doble,libre,pobre,dulce,breve,firme,noble,simple,suave,humilde,rebelde,salvaje,cobarde",
        "SENTENCE_TOO_LONG": "30",
        "TOO_MANY_COMMAS": "4 10",
        "WORD_DENSITY": "3 12",
        "SUBJUNCTIVE_AFTER": "para que|sin que|antes de que|a fin de que|con tal de que|a menos que|en caso de que|ojalá que",
        "SUBJUNCTIVE_BAD_ENDINGS": "aba,aban,abas,ía,ían,ías,aré,arás,será,serán,ienes,iene,uedes,uede,aces,ace,ienen",
        "TENSE_PAST_ENDINGS": "aba,aban,ábamos,aron,ieron,aste,iste,amos",
        "TENSE_PRESENT_MARKERS": "hoy,ahora,actualmente,estoy,estás,está,estamos,están",
    },
    "fr": {
        "PUNCT_NO_SPACE_BEFORE": ",.)]}",
        "PUNCT_SPACE_AFTER": ",;:",
        "PUNCT_NO_SPACE_AFTER": "(",
        "PUNCT_NO_DOUBLE": ",;",
        "EXCESSIVE_PUNCT": "2",
        "NBSP_REQUIRED": ";:!?",
        "WORD_CAP": "france,paris,europe,français,anglais,allemand",
        "WORD_LOWER": "janvier,février,mars,avril,mai,juin,juillet,août,septembre,octobre,novembre,décembre,lundi,mardi,mercredi,jeudi,vendredi,samedi,dimanche",
        "WORD_UPPER": "ue,onu,otan,pib,tva,adn,usb,pdf,html,url",
        "ALL_CAPS_WORD": "6",
        "REPEAT_WORD_SKIP": "le,la,les,de,du,des,et,ou,à,ni,ha,hé",
        "BRACKET_PAIRS": "()[]{}«»",
        "ARTICLE_FEM": "la,une,les",
        "ARTICLE_MASC": "le,un,les",
        "SENTENCE_TOO_LONG": "30",
        "TOO_MANY_COMMAS": "4 10",
        "WORD_DENSITY": "3 12",
    },
    "de": {
        "PUNCT_NO_SPACE_BEFORE": ",.;:!?)",
        "PUNCT_SPACE_AFTER": ",;:",
        "PUNCT_NO_SPACE_AFTER": "(",
        "PUNCT_NO_DOUBLE": ",;",
        "EXCESSIVE_PUNCT": "2",
        "WORD_CAP": "montag,dienstag,mittwoch,donnerstag,freitag,samstag,sonntag,januar,februar,märz,april,mai,juni,juli,august,september,oktober,november,dezember,deutschland,berlin,europa",
        "WORD_UPPER": "eu,uno,nato,bip,mwst,dna,usb,pdf,html,url",
        "ALL_CAPS_WORD": "6",
        "REPEAT_WORD_SKIP": "der,die,das,und,oder,zu,ha,so",
        "BRACKET_PAIRS": "()[]{}„“",
        "ARTICLE_FEM": "die,eine",
        "ARTICLE_MASC": "der,ein",
        "SENTENCE_TOO_LONG": "30",
        "TOO_MANY_COMMAS": "4 10",
        "WORD_DENSITY": "3 12",
    },
    "it": {
        "PUNCT_NO_SPACE_BEFORE": ",.;:!?)",
        "PUNCT_SPACE_AFTER": ",;:",
        "PUNCT_NO_SPACE_AFTER": "(",
        "PUNCT_NO_DOUBLE": ",;",
        "EXCESSIVE_PUNCT": "2",
        "WORD_CAP": "italia,roma,milano,europa",
        "WORD_LOWER": "gennaio,febbraio,marzo,aprile,maggio,giugno,luglio,agosto,settembre,ottobre,novembre,dicembre,lunedì,martedì,mercoledì,giovedì,venerdì,sabato,domenica",
        "WORD_UPPER": "ue,onu,nato,pil,iva,dna,usb,pdf,html,url",
        "ALL_CAPS_WORD": "6",
        "REPEAT_WORD_SKIP": "il,la,lo,gli,le,di,e,o,a,in,ha",
        "BRACKET_PAIRS": "()[]{}«»",
        "ARTICLE_FEM": "la,una,le",
        "ARTICLE_MASC": "il,lo,un,uno,gli,i",
        "FEM_ENDINGS": "a,e",
        "MASC_ENDINGS": "o,i",
        "MASC_EXCEPTION_ENDINGS": "ma",
        "AGREEMENT_EXCEPTIONS": "mano,foto,moto,radio,giorno,mappa,sofà,pianeta,poeta,problema,idioma,tema,sistema,programma,clima,grande,verde,triste,allegro,forte,intelligente,importante,possibile,amabile,libero,povero,dolce,breve,fermo,nobile,semplice,soave",
        "SENTENCE_TOO_LONG": "30",
        "TOO_MANY_COMMAS": "4 10",
        "WORD_DENSITY": "3 12",
    },
    "pt": {
        "PUNCT_NO_SPACE_BEFORE": ",.;:!?)",
        "PUNCT_SPACE_AFTER": ",;:",
        "PUNCT_NO_SPACE_AFTER": "(",
        "PUNCT_NO_DOUBLE": ",;",
        "EXCESSIVE_PUNCT": "2",
        "WORD_CAP": "portugal,brasil,lisboa,europa",
        "WORD_LOWER": "janeiro,fevereiro,março,abril,maio,junho,julho,agosto,setembro,outubro,novembro,dezembro,segunda,terça,quarta,quinta,sexta,sábado,domingo",
        "WORD_UPPER": "ue,onu,otan,pib,iva,adn,usb,pdf,html,url",
        "ALL_CAPS_WORD": "6",
        "REPEAT_WORD_SKIP": "a,o,de,que,e,ou,em,ha",
        "BRACKET_PAIRS": "()[]{}«»",
        "ARTICLE_FEM": "a,uma,as",
        "ARTICLE_MASC": "o,um,os",
        "FEM_ENDINGS": "a,as",
        "MASC_ENDINGS": "o,os",
        "MASC_EXCEPTION_ENDINGS": "ma",
        "AGREEMENT_EXCEPTIONS": "mão,foto,moto,rádio,dia,mapa,sofá,planeta,poeta,problema,idioma,tema,sistema,programa,clima,grande,verde,triste,alegre,forte,inteligente,importante,possível,amável,livre,pobre,doce,breve,firme,nobre,simples,suave",
        "SENTENCE_TOO_LONG": "30",
        "TOO_MANY_COMMAS": "4 10",
        "WORD_DENSITY": "3 12",
    },
}


# ------------------------------------------------------------------
# ID-based mapping: rules we can flip on wholesale by their LT id
# ------------------------------------------------------------------

ID_TO_DIRECTIVE = {
    # spacing / punctuation
    "COMMA_WHITESPACE":              ("PUNCT_SPACE_AFTER", ",;:"),
    "WHITESPACE_BEFORE_PUNCTUATION": ("PUNCT_NO_SPACE_BEFORE", ",.;:!?)"),
    "NO_SPACE_BEFORE_PUNCTUATION":   ("PUNCT_NO_SPACE_BEFORE", ",.;:!?)"),
    "DOUBLE_PUNCTUATION":            ("PUNCT_NO_DOUBLE", ".,;:!?"),
    "MULTIPLE_WHITESPACES":          ("SPACE_MULTIPLE", None),
    "WHITESPACE_RULE":               ("SPACE_MULTIPLE", None),
    "UPPERCASE_SENTENCE_START":      ("CAPITALIZE_SENTENCE", None),
    "SENTENCE_START":                ("CAPITALIZE_SENTENCE", None),
    "UNPAIRED_BRACKETS":             ("BRACKET_PAIRS", "()[]{}"),
    "FRENCH_WHITESPACE":             ("NBSP_REQUIRED", ";:!?"),
    "SPACE_BEFORE_PUNCTUATION":      ("NBSP_REQUIRED", ";:!?"),
    "DASH_RULE":                     ("DOUBLE_DASH", None),
    "DASH":                          ("DOUBLE_DASH", None),
    "THREE_DOTS":                    ("TRIPLE_DOT", None),
    "ELLIPSIS":                      ("TRIPLE_DOT", None),
    "STRAIGHT_QUOTES":               ("STRAIGHT_QUOTES", None),
    "COMMA_BEFORE_DOT":              ("COMMA_BEFORE_DOT", None),
    "TRAILING_DOTS":                 ("TRAILING_DOTS", None),
    "EXCESSIVE_PUNCT":               ("EXCESSIVE_PUNCT", "2"),
    "EXCESSIVE_PUNCTUATION":         ("EXCESSIVE_PUNCT", "2"),
    "REPEAT_WORD":                   ("REPEAT_WORD", None),
    "REPEATED_WORDS":                ("REPEAT_WORD", None),
    "REPEATED_WORDS_3X":             ("REPEAT_WORD", None),
    "REPEATED_PHRASE":               ("REPEAT_PHRASE", None),
    "REPEATED_WORDS_IN_SHORT_TEXT":  ("REPEAT_PHRASE", None),
    "REP_USO":                       ("REPEAT_WORD", None),
    "REP_CREER":                     ("REPEAT_WORD", None),
    "REP_DECIR":                     ("REPEAT_WORD", None),
    "REP_HACER":                     ("REPEAT_WORD", None),
    "REP_PENSAR":                    ("REPEAT_WORD", None),
    "REP_SABER":                     ("REPEAT_WORD", None),
    "REP_VER":                       ("REPEAT_WORD", None),
    "REP_IR":                        ("REPEAT_WORD", None),
    "REP_VENIR":                     ("REPEAT_WORD", None),
    "REP_PODER":                     ("REPEAT_WORD", None),
    "REP_QUERER":                    ("REPEAT_WORD", None),
    "SENTENCE_TOO_LONG":             ("SENTENCE_TOO_LONG", "30"),
    "TOO_MANY_COMMAS":               ("TOO_MANY_COMMAS", "4 10"),
    "WORD_DENSITY":                  ("WORD_DENSITY", "3 12"),
    "ALL_CAPS":                      ("ALL_CAPS_WORD", "6"),
    "DOUBLES_ESPACES":               ("SPACE_MULTIPLE", None),
    "DEUX_POINTS_ESPACE":            ("NBSP_REQUIRED", ":"),
    "POINT_DOUBLE":                  ("PUNCT_NO_DOUBLE", "."),
    "NO_SPACE_CLOSING_QUOTE":        ("QUOTE_SPACING", None),
    "PUNCTUATE_QUOTATIONS":          ("QUOTE_SPACING", None),
    "UPPERCASE_AFTER_COMMA":         ("CAPITALIZE_SENTENCE", None),
    "WRONG_DASH":                    ("DOUBLE_DASH", None),
    "WEEK_LONG_HYPHEN":              ("DOUBLE_DASH", None),
    "SPACE_COMPOUNDS":               ("DOUBLE_DASH", None),
    "CONFUSION_I_EXCLAMACION":       ("SENTENCE_OPEN_ES", "!"),
    "QUESTION_WITHOUT_VERB":         ("SENTENCE_OPEN_ES", "?"),

    # Spanish-specific rules
    "MAYUSCULAS_INICIO_FRASE":       ("CAPITALIZE_SENTENCE", None),
    "MIN_DIAS_SEMANA":               ("WORD_LOWER", None),
    "MIN_ESTACIONES":                ("WORD_LOWER", None),
}


# ------------------------------------------------------------------
# Word-list extraction: rule IDs that contain literal word lists
# Map LT rule ID -> (gramcheck directive, extraction mode)
# ------------------------------------------------------------------

WORDLIST_ID_TO_TYPE = {
    # Spanish: days and months that should be lowercase mid-sentence
    "MIN_DIAS_SEMANA":         ("WORD_LOWER", "token"),
    "MIN_ESTACIONES":          ("WORD_LOWER", "token"),

    # German: days and months are capitalized (WORD_CAP)
    "WOCHENTAGE":              ("WORD_CAP", "token"),
    "MONATS_NAMEN":            ("WORD_CAP", "token"),

    # Add more language-specific IDs here as they are discovered
    # LanguageTool does not use consistent IDs for these lists across languages
}

# ------------------------------------------------------------------
# XML helpers
# ------------------------------------------------------------------

def get_local(tag) -> str:
    """Strip namespace prefix from an ElementTree tag."""
    # Handle both string tags (ElementTree) and lxml tags
    if hasattr(tag, 'tag'):
        tag = tag.tag

    if isinstance(tag, str):
        return tag.split("}", 1)[-1] if "}" in tag else tag

    return str(tag)


def iter_children(elem, name: str):
    for c in elem:
        if get_local(c.tag) == name:
            yield c


def text_of(elem) -> str:
    return "".join(elem.itertext()).strip()


# ------------------------------------------------------------------
# Extraction of PAIR (simple confusion) rules
# ------------------------------------------------------------------

def extract_simple_pair(rule) -> tuple[str, str, str] | None:
    """
    Look for a <pattern> with 1 or 2 plain literal <token> elements
    and a <message><suggestion> with plain literal text. This captures
    simple word confusions, missing diacritics and common 2-word phrases.
    Returns (src, dst, msg) or None.
    """
    pattern = None
    message = None

    for child in rule:
        n = get_local(child.tag)

        if n == "pattern":
            pattern = child
        elif n == "message":
            message = child

    if pattern is None or message is None:
        return None

    # Rules with <marker> replace only the marked span, so pairing the whole
    # pattern with the suggestion produces corrupt entries -- skip them
    for child in pattern:
        if get_local(child.tag) == "marker":
            return None

    tokens = list(iter_children(pattern, "token"))

    if len(tokens) < 1 or len(tokens) > 2:
        return None

    # All tokens must be plain literals (no postag, regexp, etc.)
    for t in tokens:
        if any(k in t.attrib for k in ("postag", "chunk", "regexp", "skip", "min", "max")):
            return None
        if len(list(t)) > 0:  # inner elements (exception/etc)
            return None

    # Build src from the literal token(s)
    parts = []

    for t in tokens:
        txt = (t.text or "").strip()

        if not txt:
            return None

        parts.append(txt)

    src = " ".join(parts)

    if not re.match(r"^[A-Za-z\u00C0-\u017F']+(?:\s[A-Za-z\u00C0-\u017F']+)?$", src):
        return None

    # <suggestion> may have multiple, take the first plain-text one
    dst = None

    for sugg in iter_children(message, "suggestion"):
        st = text_of(sugg)

        # strip any <match .../> refs - if present, skip
        if list(iter_children(sugg, "match")):
            continue

        if st and re.match(r"^[A-Za-z0-9\u00C0-\u017F' \-]+$", st):
            dst = st
            break

    if not dst:
        return None

    # Compact message
    msg = text_of(message)
    msg = re.sub(r"\s+", " ", msg).strip()

    if len(msg) > 80:
        msg = msg[:77] + "..."

    return (src, dst, msg)


# ------------------------------------------------------------------
# Punctuation rule extraction from token attributes
# ------------------------------------------------------------------

def extract_punctuation_rule(rule) -> tuple[str, str | None] | None:
    """
    Detect simple punctuation rules from token attributes.
    Returns (directive, data) or None.
    """
    pattern = None

    for child in rule:
        if get_local(child.tag) == "pattern":
            pattern = child
            break

    if pattern is None:
        return None

    tokens = list(iter_children(pattern, "token"))

    if len(tokens) != 2:
        return None

    t1, t2 = tokens
    txt1 = (t1.text or "").strip()
    txt2 = (t2.text or "").strip()

    # Space before punctuation (e.g. "word :" -> "word:")
    if t1.attrib.get("spacebefore", "") == "no" and len(txt2) == 1 and txt2 in ",.;:!?":
        return ("PUNCT_NO_SPACE_BEFORE", txt2)

    # Space after punctuation (e.g. ": word" -> ": word")
    if t2.attrib.get("spacebefore", "") == "no" and len(txt1) == 1 and txt1 in ",;:":
        return ("PUNCT_SPACE_AFTER", txt1)

    return None


# ------------------------------------------------------------------
# Extraction of word lists (days, months, articles, etc)
# ------------------------------------------------------------------

def extract_wordlist(rule, mode: str) -> list[str] | None:
    """
    Extract a list of literal words from a rule's <pattern>.
    mode='token' collects text from every plain <token> child.
    Returns a list of words or None if the rule is not suitable.
    """
    pattern = None

    for child in rule:
        if get_local(child.tag) == "pattern":
            pattern = child
            break

    if pattern is None:
        return None

    words = []

    for tok in iter_children(pattern, "token"):
        if any(k in tok.attrib for k in ("postag", "chunk", "regexp", "skip", "min", "max")):
            continue

        if len(list(tok)) > 0:
            continue

        txt = (tok.text or "").strip()

        if txt and re.match(r"^[A-Za-z\u00C0-\u017F' ]+$", txt):
            words.append(txt)

    return words if words else None


# ------------------------------------------------------------------
# Entity list extraction from LanguageTool entities.ent
# ------------------------------------------------------------------

def parse_entities_ent(path: str) -> dict[str, list[str]]:
    """
    Parse a LanguageTool entities.ent file and extract useful word lists.
    Returns a dict mapping gramcheck directive -> list of words.
    """
    result: dict[str, list[str]] = {}

    if not path or not os.path.exists(path):
        return result

    with open(path, 'r', encoding='utf-8') as f:
        text = f.read()

    # Entity name -> gramcheck directive
    entity_map = {
        'dias_semana': 'WORD_LOWER',
        'dias_semana_may': 'WORD_CAP',
        'meses_ano': 'WORD_LOWER',
        'meses_ano_may': 'WORD_CAP',
        'estaciones_ano': 'WORD_LOWER',
        'gentilicios': 'WORD_CAP',
        'partes_dia': 'WORD_LOWER',
    }

    for entity_name, directive in entity_map.items():
        # Entities may span multiple lines
        pattern = rf'<!ENTITY\s+{entity_name}\s+"([^"]+)"'
        m = re.search(pattern, text, re.DOTALL)

        if not m:
            continue

        raw = m.group(1)

        # Remove regex metacharacters
        raw = re.sub(r'\[.*?\]', '', raw)
        raw = raw.replace('?', '').replace('*', '').replace('+', '')
        raw = raw.replace('(?-i)', '').replace('(?i)', '')
        raw = raw.replace(r'\b', '').replace(r'\s', '').replace(r'\p{L}', '')
        raw = raw.replace('[^', '').replace(']', '')

        words = []

        for part in raw.split('|'):
            part = part.strip()

            if not part:
                continue

            # Skip entries with regex chars or numbers
            if re.search(r'[\^$\[\]().\\0-9]', part):
                continue

            words.append(part)

        if words:
            if directive not in result:
                result[directive] = []

            result[directive].extend(words)

    return result


def parse_grammar(path: str, entities_path: str | None = None):
    if LXML_AVAILABLE:
        # Use lxml with entity resolution disabled
        parser = ET.XMLParser(resolve_entities=False, load_dtd=False, no_network=True)
        tree = ET.parse(path, parser=parser)
        root = tree.getroot()
    else:
        # Fallback: read file and handle entities manually
        with open(path, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Remove XML declaration and entire DOCTYPE section
        content = re.sub(r'<\?xml[^>]*\?>', '', content)
        content = re.sub(r'<!DOCTYPE[^>]*\]>', '', content, flags=re.DOTALL)
        
        # Replace all entity patterns with valid placeholders
        content = re.sub(r'&[a-zA-Z_]+;', 'ENTITY_PLACEHOLDER', content)
        
        # Parse the modified content
        root = ET.fromstring(content)

    id_hits: dict[str, tuple[str, str | None]] = {}
    pairs: list[tuple[str, str, str]] = []
    seen_pairs: set[tuple[str, str]] = set()
    wordlists: dict[str, list[str]] = {}

    # LT rules live inside <category><rule id="...">...</rule></category>
    # and also inline <rule>. Walk everything
    for rule in root.iter():
        if get_local(rule.tag) != "rule":
            continue

        rid = rule.attrib.get("id", "")

        if rid in ID_TO_DIRECTIVE:
            directive, data = ID_TO_DIRECTIVE[rid]
            id_hits[directive] = (directive, data)

        if rid in WORDLIST_ID_TO_TYPE:
            directive, mode = WORDLIST_ID_TO_TYPE[rid]
            words = extract_wordlist(rule, mode)

            if words:
                if directive not in wordlists:
                    wordlists[directive] = []

                wordlists[directive].extend(words)

        # try to derive punctuation rule from token attributes
        punct = extract_punctuation_rule(rule)
        
        if punct:
            directive, data = punct
            id_hits[directive] = (directive, data)

        # try to derive a PAIR
        got = extract_simple_pair(rule)

        if got:
            src, dst, msg = got
            key = (src.lower(), dst.lower())

            if key not in seen_pairs:
                seen_pairs.add(key)
                pairs.append((src, dst, msg))

    if entities_path:
        ent_lists = parse_entities_ent(entities_path)

        for directive, words in ent_lists.items():
            if directive not in wordlists:
                wordlists[directive] = []

            wordlists[directive].extend(words)

    return id_hits, pairs, wordlists


def emit(id_hits, pairs, wordlists, lang: str, out):
    print(f"# gramcheck rule pack -- generated from LanguageTool by lt2rul.py", file=out)
    print(f"# LanguageTool rules are LGPL; keep this file with its notice.", file=out)
    print(f"LANG   {lang}", file=out)
    print(f"NAME   {lang} (from LanguageTool)", file=out)
    print("", file=out)

    # Merge manual language defaults with generated directives, avoiding duplicates
    # Precedence: generated data > generated wordlists > manual wordlists
    merged: dict[str, str] = {}
    if lang in MANUAL_WORDLISTS:
        merged.update(MANUAL_WORDLISTS[lang])

    for directive, words in wordlists.items():
        # Merge generated wordlists with manual ones, deduplicating words
        existing = set()
        existing_list = []

        if directive in merged:
            for w in merged[directive].split(','):
                w = w.strip()

                if w and w.lower() not in existing:
                    existing.add(w.lower())
                    existing_list.append(w)

        for w in words:
            if w.lower() not in existing:
                existing.add(w.lower())
                existing_list.append(w)

        merged[directive] = ",".join(existing_list)

    for directive, data in id_hits.values():
        merged[directive] = data if data else ""

    for directive, data in merged.items():
        if data:
            print(f"{directive} {data}", file=out)
        else:
            print(directive, file=out)

    print("", file=out)
    print(f"# {len(pairs)} PAIR entries", file=out)

    for src, dst, msg in sorted(pairs, key=lambda x: x[0].lower()):
        # src and dst may contain spaces -> quote them
        src_out = f'"{src.lower()}"' if " " in src else src.lower()
        dst_out = f'"{dst}"' if " " in dst else dst

        line = f"PAIR  {src_out:<20s} {dst_out}"

        if msg:
            line += f"  info  # {msg}"

        print(line, file=out)


def main() -> int:
    ap = argparse.ArgumentParser(description="LanguageTool grammar.xml -> gramcheck .rul")
    ap.add_argument("xml_files", nargs="+", help="One or more LanguageTool XML rule files")
    ap.add_argument("lang", help="ISO code, e.g. en, es, de, fr, pt, it")
    ap.add_argument("--out", "-o", default="-")
    ns = ap.parse_args()

    id_hits: dict[str, tuple[str, str | None]] = {}
    pairs: list[tuple[str, str, str]] = []
    wordlists: dict[str, list[str]] = {}

    # Derive entities.ent path from first XML file
    entities_path = None
    if ns.xml_files:
        xml_dir = os.path.dirname(ns.xml_files[0])

        # LanguageTool layout: rules/<lang>/grammar.xml -> resource/<lang>/entities.ent
        parts = xml_dir.split(os.sep)

        if 'rules' in parts:
            lang_idx = parts.index('rules') + 1

            if lang_idx < len(parts):
                lang = parts[lang_idx]
                base = os.path.dirname(os.path.dirname(xml_dir))
                entities_path = os.path.join(base, 'resource', lang, 'entities.ent')

    for xml_path in ns.xml_files:
        h, p, w = parse_grammar(xml_path, entities_path)
        id_hits.update(h)
        pairs.extend(p)

        for directive, words in w.items():
            if directive not in wordlists:
                wordlists[directive] = []

            wordlists[directive].extend(words)

    # Add manual fallback pairs, deduplicating against generated ones
    seen = set((src.lower(), dst.lower()) for src, dst, _ in pairs)

    for src, dst in MANUAL_PAIRS.get(ns.lang, []):
        key = (src.lower(), dst.lower())
        
        if key not in seen:
            seen.add(key)
            pairs.append((src, dst, ""))

    out = sys.stdout if ns.out == "-" else open(ns.out, "w", encoding="utf-8")
    
    try:
        emit(id_hits, pairs, wordlists, ns.lang, out)
    finally:
        if out is not sys.stdout:
            out.close()

    print(f"lt2rul: {len(id_hits)} directives, {len(pairs)} pairs, {len(wordlists)} wordlists", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
