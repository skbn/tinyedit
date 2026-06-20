/*
 * tinyedit - Text editor for AmigaOS
 *
 * Copyright (C) 2026 Tanausú M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */

#include "thes.h"
#include "../spell/spell.h" /* SpellChecker*, spell_stem/generate/free_list */

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_MYTHES
#include <cstdio>
#include <new>
#include <dirent.h>
#include <sys/types.h>
#include <mythes.hxx>

/* Portable strdup for C++ - returns NULL on OOM or NULL input */
static char *ce_strdup(const char *s)
{
    size_t n;
    char *r;

    if (!s)
        return NULL;

    n = strlen(s) + 1;
    r = static_cast<char *>(malloc(n));

    if (!r)
        return NULL;

    memcpy(r, s, n);
    return r;
}
#endif

extern "C"
{
    struct ThesHandle
    {
#ifdef HAVE_MYTHES
        MyThes *mt;
        SpellChecker *sc; /* borrowed, may be NULL */
#else
        int dummy;
#endif
    };

    ThesHandle *thes_new(const char *idx_path, const char *dat_path)
    {
#ifdef HAVE_MYTHES
        ThesHandle *t;
        FILE *fp;

        if (!idx_path || !dat_path)
            return NULL;

        /* Probe files first - MyThes ctor returns broken object on missing files */
        fp = fopen(idx_path, "rb");

        if (!fp)
            return NULL;

        fclose(fp);

        fp = fopen(dat_path, "rb");

        if (!fp)
            return NULL;

        fclose(fp);

        t = static_cast<ThesHandle *>(calloc(1, sizeof(*t)));

        if (!t)
            return NULL;

        t->mt = new (std::nothrow) MyThes(idx_path, dat_path);

        if (!t->mt)
        {
            free(t);
            return NULL;
        }

        return t;
#else
        return NULL;
#endif
    }

    void thes_free(ThesHandle *t)
    {
        if (!t)
            return;

#ifdef HAVE_MYTHES
        if (t->mt)
            delete t->mt;
#endif

        free(t);
    }

    void thes_set_speller(ThesHandle *t, SpellChecker *sc)
    {
#ifdef HAVE_MYTHES
        if (t)
            t->sc = sc;
#endif
    }

#ifdef HAVE_MYTHES
    /* Direct MyThes lookup with deep copy. Returns count or -1 on OOM. Inflects if needed */
    static int thes_direct_lookup(ThesHandle *t, const char *word, ThesMeaning **out_meanings, const char *inflect_example)
    {
        mentry *me;
        int n, i, j;
        ThesMeaning *out;

        *out_meanings = NULL;

        n = t->mt->Lookup(word, static_cast<int>(strlen(word)), &me);

        if (n <= 0)
            return 0;

        out = static_cast<ThesMeaning *>(calloc(static_cast<size_t>(n), sizeof(ThesMeaning)));

        if (!out)
        {
            t->mt->CleanUpAfterLookup(&me, n);
            return -1;
        }

        for (i = 0; i < n; i++)
        {
            out[i].def = ce_strdup(me[i].defn ? me[i].defn : "");
            out[i].nsyns = 0;
            out[i].syns = NULL;

            if (me[i].count > 0)
            {
                out[i].syns = static_cast<char **>(calloc(static_cast<size_t>(me[i].count), sizeof(char *)));

                if (out[i].syns)
                {
                    for (j = 0; j < me[i].count; j++)
                    {
                        const char *raw = me[i].psyns[j] ? me[i].psyns[j] : "";
                        char *cooked = NULL;

                        /* Try inflection if speller attached - use first generated form */
                        if (inflect_example && t->sc && raw[0])
                        {
                            char **gen = NULL;
                            int ng = spell_generate(t->sc, raw, inflect_example, &gen);

                            if (ng > 0 && gen && gen[0])
                                cooked = ce_strdup(gen[0]);

                            if (gen)
                                spell_free_list(t->sc, gen, ng > 0 ? ng : 0);
                        }

                        if (!cooked)
                            cooked = ce_strdup(raw);

                        out[i].syns[j] = cooked;
                    }

                    out[i].nsyns = me[i].count;
                }
            }
        }

        t->mt->CleanUpAfterLookup(&me, n);

        *out_meanings = out;
        return n;
    }

    /* Append add[] to acc[]. On OOM frees both (leak-free but unrecoverable) */
    static int thes_meanings_append(ThesMeaning **acc, int *accn, ThesMeaning *add, int addn)
    {
        ThesMeaning *grown;
        int i;

        if (addn <= 0)
            return 1;

        if (!add)
            return 0;

        grown = static_cast<ThesMeaning *>(realloc(*acc, static_cast<size_t>(*accn + addn) * sizeof(ThesMeaning)));

        if (!grown)
        {
            /* Free both halves to avoid leaks */
            for (i = 0; i < *accn; i++)
            {
                free((*acc)[i].def);

                if ((*acc)[i].syns)
                {
                    int k;

                    for (k = 0; k < (*acc)[i].nsyns; k++)
                        free((*acc)[i].syns[k]);

                    free((*acc)[i].syns);
                }
            }

            free(*acc);

            *acc = NULL;
            *accn = 0;

            for (i = 0; i < addn; i++)
            {
                free(add[i].def);

                if (add[i].syns)
                {
                    int k;

                    for (k = 0; k < add[i].nsyns; k++)
                        free(add[i].syns[k]);

                    free(add[i].syns);
                }
            }

            free(add);
            return 0;
        }

        *acc = grown;

        for (i = 0; i < addn; i++)
            (*acc)[*accn + i] = add[i];

        *accn += addn;

        free(add);

        return 1;
    }
#endif

    int thes_lookup(ThesHandle *t, const char *word, ThesMeaning **out_meanings)
    {
#ifdef HAVE_MYTHES
        int n;
        ThesMeaning *acc = NULL;
        int acc_n = 0;

        if (out_meanings)
            *out_meanings = NULL;

        if (!t || !t->mt || !word || !word[0] || !out_meanings)
            return -1;

        /* Direct lookup first - fast path */
        n = thes_direct_lookup(t, word, &acc, NULL);

        if (n < 0)
            return -1;

        if (n > 0)
        {
            acc_n = n;
            *out_meanings = acc;
            return acc_n;
        }

        /* Not found verbatim - try Hunspell_stem()  */
        if (t->sc)
        {
            char **stems = NULL;
            int ns = spell_stem(t->sc, word, &stems);
            int i;

            for (i = 0; i < ns; i++)
            {
                ThesMeaning *more = NULL;
                int nm;

                if (!stems[i] || !stems[i][0])
                    continue;

                /* Skip if stem == word to avoid redundant lookup */
                if (strcmp(stems[i], word) == 0)
                    continue;

                nm = thes_direct_lookup(t, stems[i], &more, word);

                if (nm > 0 && more)
                {
                    if (!thes_meanings_append(&acc, &acc_n, more, nm))
                    {
                        /* acc was freed inside append on OOM */
                        spell_free_list(t->sc, stems, ns);
                        return -1;
                    }
                }
                else if (more)
                {
                    /* nm == 0 but more allocated - shouldn't happen */
                    thes_free_meanings(t, more, nm);
                }
            }

            if (stems)
                spell_free_list(t->sc, stems, ns > 0 ? ns : 0);
        }

        *out_meanings = acc;

        return acc_n;
#else

        if (out_meanings)
            *out_meanings = NULL;

        return -1;
#endif
    }

    void thes_free_meanings(ThesHandle *t, ThesMeaning *m, int nmeanings)
    {
        int i;
        int j;

        if (!m)
            return;

        for (i = 0; i < nmeanings; i++)
        {
            if (m[i].def)
                free(m[i].def);

            if (m[i].syns)
            {
                for (j = 0; j < m[i].nsyns; j++)
                {
                    if (m[i].syns[j])
                        free(m[i].syns[j]);
                }

                free(m[i].syns);
            }
        }

        free(m);
    }

    const char *thes_get_encoding(ThesHandle *t)
    {
#ifdef HAVE_MYTHES
        if (t && t->mt)
            return t->mt->get_th_encoding();

        return NULL;
#else
        return NULL;
#endif
    }

    int thes_is_available(void)
    {
#ifdef HAVE_MYTHES
        return 1;
#else
        return 0;
#endif
    }

    char **thes_list_dictionaries(const char *dir_path, int *n_dicts)
    {
#ifdef HAVE_MYTHES
        char **result = NULL;
        int count = 0;
        int capacity = 0;
        DIR *dir;
        struct dirent *entry;

        if (!dir_path || !n_dicts)
            return NULL;

        *n_dicts = 0;

        dir = opendir(dir_path);

        if (!dir)
            return NULL;

        while ((entry = readdir(dir)) != NULL)
        {
            const char *name = entry->d_name;
            size_t len = strlen(name);

            /* Look for th_*.idx files (MyThes convention) */
            if (len > 8 && strncmp(name, "th_", 3) == 0 && strcmp(name + len - 4, ".idx") == 0)
            {
                /* Extract dictionary name: th_es_ES_v2.idx -> es_ES */
                char *dict_name;
                size_t start = 3;     /* after "th_" */
                size_t end = len - 4; /* before ".idx" */

                /* Handle _v2 suffix */
                if (end > 3 && strcmp(name + end - 3, "_v2") == 0)
                    end -= 3;

                if (end <= start)
                    continue;

                dict_name = static_cast<char *>(malloc(end - start + 1));

                if (!dict_name)
                    continue;

                memcpy(dict_name, name + start, end - start);
                dict_name[end - start] = '\0';

                /* Add to array */
                if (count >= capacity)
                {
                    int new_cap = (capacity == 0) ? 16 : capacity * 2;
                    char **new_result = static_cast<char **>(realloc(result, new_cap * sizeof(char *)));

                    if (!new_result)
                    {
                        free(dict_name);
                        continue;
                    }

                    result = new_result;
                    capacity = new_cap;
                }

                result[count++] = dict_name;
            }
        }

        closedir(dir);

        if (count == 0)
        {
            if (result)
                free(result);

            return NULL;
        }

        *n_dicts = count;

        return result;
#else

        if (n_dicts)
            *n_dicts = 0;

        return NULL;
#endif
    }

    void thes_free_dictionaries(char **dicts, int n_dicts)
    {
        int i;

        if (!dicts)
            return;

        for (i = 0; i < n_dicts; i++)
        {
            if (dicts[i])
                free(dicts[i]);
        }

        free(dicts);
    }

} /* extern "C" */
