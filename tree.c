// Add this to tree.c — replace the existing tree_from_index stub
// Also add this #include at the top of tree.c:
#include "index.h"

// Forward declaration needed (object_write is in object.c)
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

/*
 * write_tree_recursive:
 *   entries     - the full index entry array
 *   count       - number of entries in this slice
 *   prefix      - the directory prefix we're currently processing (e.g., "src/")
 *   id_out      - where to store the written tree's ObjectID
 *
 * Strategy:
 *   Walk the entries. For each entry whose path starts with `prefix`:
 *     - Strip the prefix to get the relative path (e.g., "src/main.c" -> "main.c")
 *     - If the relative path contains no '/', it's a direct file: add a BLOB entry
 *     - If it contains a '/', the part before the first '/' is a subdirectory:
 *       collect all entries sharing that subdir prefix and recurse.
 *   After processing all unique names, serialize and write the tree.
 */
static int write_tree_recursive(const IndexEntry *entries, int count,
                                 const char *prefix, ObjectID *id_out) {
    Tree tree;
    tree.count = 0;

    size_t prefix_len = strlen(prefix);
    int i = 0;

    while (i < count) {
        const char *path = entries[i].path;

        // Skip entries that don't belong to this prefix
        if (strncmp(path, prefix, prefix_len) != 0) {
            i++;
            continue;
        }

        // relative = path after the prefix (e.g., "main.c" or "sub/util.c")
        const char *relative = path + prefix_len;
        const char *slash = strchr(relative, '/');

        if (!slash) {
            // Direct file entry — add as BLOB
            TreeEntry *entry = &tree.entries[tree.count++];
            entry->mode = entries[i].mode;
            entry->hash = entries[i].hash;
            strncpy(entry->name, relative, sizeof(entry->name) - 1);
            entry->name[sizeof(entry->name) - 1] = '\0';
            i++;
        } else {
            // Subdirectory — get the directory name (e.g., "src")
            size_t dir_name_len = (size_t)(slash - relative);
            char dir_name[256];
            if (dir_name_len >= sizeof(dir_name)) return -1;
            memcpy(dir_name, relative, dir_name_len);
            dir_name[dir_name_len] = '\0';

            // Build the full prefix for the recursive call (e.g., "src/")
            char sub_prefix[512];
            snprintf(sub_prefix, sizeof(sub_prefix), "%s%s/", prefix, dir_name);

            // Recurse to build the subtree
            ObjectID sub_tree_id;
            if (write_tree_recursive(entries, count, sub_prefix, &sub_tree_id) != 0)
                return -1;

            // Add this subtree as a TREE entry
            TreeEntry *entry = &tree.entries[tree.count++];
            entry->mode = 0040000;
            entry->hash = sub_tree_id;
            strncpy(entry->name, dir_name, sizeof(entry->name) - 1);
            entry->name[sizeof(entry->name) - 1] = '\0';

            // Skip all entries that belong to this subdirectory
            while (i < count && strncmp(entries[i].path, sub_prefix, strlen(sub_prefix)) == 0)
                i++;
        }
    }

    // Serialize and write the tree object
    void *data;
    size_t len;
    if (tree_serialize(&tree, &data, &len) != 0) return -1;

    int rc = object_write(OBJ_TREE, data, len, id_out);
    free(data);
    return rc;
}

int tree_from_index(ObjectID *id_out) {
    Index index;
    if (index_load(&index) != 0) return -1;

    // Start recursion at the root prefix (empty string = no prefix stripping)
    return write_tree_recursive(index.entries, index.count, "", id_out);
}
