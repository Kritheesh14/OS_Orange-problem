// index.c — replace the three TODO functions with these implementations

// Forward declaration needed
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// ─── Comparison function for qsort (sort index entries by path) ───────────
static int compare_index_entries(const void *a, const void *b) {
    return strcmp(((const IndexEntry *)a)->path, ((const IndexEntry *)b)->path);
}

int index_load(Index *index) {
    index->count = 0;

    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) {
        // File doesn't exist yet — empty index is valid
        return 0;
    }

    char hex[HASH_HEX_SIZE + 1];
    while (index->count < MAX_INDEX_ENTRIES) {
        IndexEntry *e = &index->entries[index->count];
        // Format: <mode-octal> <64-char-hex> <mtime> <size> <path>
        int ret = fscanf(f, "%o %64s %llu %u %511s\n",
                         &e->mode,
                         hex,
                         (unsigned long long *)&e->mtime_sec,
                         &e->size,
                         e->path);
        if (ret == EOF) break;
        if (ret != 5) { fclose(f); return -1; }

        if (hex_to_hash(hex, &e->hash) != 0) { fclose(f); return -1; }
        index->count++;
    }

    fclose(f);
    return 0;
}
