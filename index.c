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

int index_save(const Index *index) {
    // Work on a mutable copy so we can sort without modifying the caller's data
    Index sorted = *index;
    qsort(sorted.entries, sorted.count, sizeof(IndexEntry), compare_index_entries);

    char tmp_path[256];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", INDEX_FILE);

    FILE *f = fopen(tmp_path, "w");
    if (!f) return -1;

    for (int i = 0; i < sorted.count; i++) {
        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&sorted.entries[i].hash, hex);
        fprintf(f, "%o %s %llu %u %s\n",
                sorted.entries[i].mode,
                hex,
                (unsigned long long)sorted.entries[i].mtime_sec,
                sorted.entries[i].size,
                sorted.entries[i].path);
    }

    // Flush userspace buffers, sync to disk, then atomically rename
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    return rename(tmp_path, INDEX_FILE);
}

int index_add(Index *index, const char *path) {
    // Step 1: Read the file contents
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open '%s'\n", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < 0) { fclose(f); return -1; }

    void *contents = malloc((size_t)file_size);
    if (!contents) { fclose(f); return -1; }

    if (fread(contents, 1, (size_t)file_size, f) != (size_t)file_size) {
        free(contents); fclose(f); return -1;
    }
    fclose(f);

    // Step 2: Write the file as a blob to the object store
    ObjectID blob_id;
    if (object_write(OBJ_BLOB, contents, (size_t)file_size, &blob_id) != 0) {
        free(contents); return -1;
    }
    free(contents);

    // Step 3: Get file metadata (mode, mtime, size)
    struct stat st;
    if (stat(path, &st) != 0) return -1;

    uint32_t mode = S_ISREG(st.st_mode)
                    ? ((st.st_mode & S_IXUSR) ? 0100755 : 0100644)
                    : 0100644;

    // Step 4: Update or insert the index entry
    IndexEntry *existing = index_find(index, path);
    if (existing) {
        // Update in-place
        existing->mode      = mode;
        existing->hash      = blob_id;
        existing->mtime_sec = (uint64_t)st.st_mtime;
        existing->size      = (uint32_t)st.st_size;
    } else {
        // Append new entry
        if (index->count >= MAX_INDEX_ENTRIES) {
            fprintf(stderr, "error: index is full\n");
            return -1;
        }
        IndexEntry *e = &index->entries[index->count++];
        e->mode      = mode;
        e->hash      = blob_id;
        e->mtime_sec = (uint64_t)st.st_mtime;
        e->size      = (uint32_t)st.st_size;
        strncpy(e->path, path, sizeof(e->path) - 1);
        e->path[sizeof(e->path) - 1] = '\0';
    }

    // Step 5: Save the index atomically
    return index_save(index);
}
