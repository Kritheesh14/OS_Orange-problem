// commit.c — replace the commit_create TODO stub with this implementation

int commit_create(const char *message, ObjectID *commit_id_out) {
    // Step 1: Build a tree from the current index
    ObjectID tree_id;
    if (tree_from_index(&tree_id) != 0) {
        fprintf(stderr, "error: failed to build tree from index\n");
        return -1;
    }

    // Step 2: Fill the Commit struct
    Commit c;
    memset(&c, 0, sizeof(c));

    c.tree = tree_id;

    // Step 3: Read the parent commit from HEAD (may not exist for first commit)
    if (head_read(&c.parent) == 0) {
        c.has_parent = 1;
    } else {
        c.has_parent = 0; // First commit — no parent
    }

    // Step 4: Set author and timestamp
    snprintf(c.author, sizeof(c.author), "%s", pes_author());
    c.timestamp = (uint64_t)time(NULL);

    // Step 5: Set the commit message
    snprintf(c.message, sizeof(c.message), "%s", message);

    // Step 6: Serialize the commit struct to text
    void *data;
    size_t len;
    if (commit_serialize(&c, &data, &len) != 0) {
        fprintf(stderr, "error: failed to serialize commit\n");
        return -1;
    }

    // Step 7: Write the commit object to the object store
    ObjectID commit_id;
    int rc = object_write(OBJ_COMMIT, data, len, &commit_id);
    free(data);
    if (rc != 0) {
        fprintf(stderr, "error: failed to write commit object\n");
        return -1;
    }

    // Step 8: Update HEAD (branch pointer) to the new commit
    if (head_update(&commit_id) != 0) {
        fprintf(stderr, "error: failed to update HEAD\n");
        return -1;
    }

    if (commit_id_out) *commit_id_out = commit_id;
    return 0;
}
