
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    unsigned int hash_len;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, id_out->hash, &hash_len);
    EVP_MD_CTX_free(ctx);
}

// Get the filesystem path where an object should be stored.
// Format: .pes/objects/XX/YYYYYYYY...
// The first 2 hex chars form the shard directory; the rest is the filename.
void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    char header[64];
    const char *type_str =
        (type == OBJ_BLOB) ? "blob" :
        (type == OBJ_TREE) ? "tree" : "commit";

    int header_len = snprintf(header, sizeof(header), "%s %zu", type_str, len) + 1;

    size_t total_len = header_len + len;
    uint8_t *full = malloc(total_len);
    if (!full) return -1;

    memcpy(full, header, header_len);
    memcpy(full + header_len, data, len);

    compute_hash(full, total_len, id_out);

    if (object_exists(id_out)) {
        free(full);
        return 0;
    }

    char final_path[512];
    object_path(id_out, final_path, sizeof(final_path));

    char dir_path[512];
    strncpy(dir_path, final_path, sizeof(dir_path));
    char *slash = strrchr(dir_path, '/');
    if (!slash) {
        free(full);
        return -1;
    }
    *slash = '\0';

    mkdir(OBJECTS_DIR, 0755);
    mkdir(dir_path, 0755);

    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s/tmpXXXXXX", dir_path);

    int fd = mkstemp(tmp_path);
    if (fd < 0) {
        free(full);
        return -1;
    }

    if (write(fd, full, total_len) != (ssize_t)total_len) {
        close(fd);
        unlink(tmp_path);
        free(full);
        return -1;
    }

    if (fsync(fd) != 0) {
        close(fd);
        unlink(tmp_path);
        free(full);
        return -1;
    }

    close(fd);

    if (rename(tmp_path, final_path) != 0) {
        unlink(tmp_path);
        free(full);
        return -1;
    }

    int dir_fd = open(dir_path, O_DIRECTORY);
    if (dir_fd >= 0) {
        fsync(dir_fd);
        close(dir_fd);
    }

    free(full);
    return 0;
}
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    char path[512];
    object_path(id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    // get file size
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return -1;
    }
    rewind(f);

    uint8_t *buf = malloc(size);
    if (!buf) {
        fclose(f);
        return -1;
    }

    if (fread(buf, 1, size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);

    // verify hash
    ObjectID computed;
    compute_hash(buf, size, &computed);
    if (memcmp(computed.hash, id->hash, HASH_SIZE) != 0) {
        free(buf);
        return -1;
    }

    // parse header: "<type> <size>\0"
    uint8_t *nullb = memchr(buf, '\0', size);
    if (!nullb) {
        free(buf);
        return -1;
    }

    size_t header_len = (nullb - buf) + 1;

    char type_str[16];
    size_t obj_size;

    if (sscanf((char *)buf, "%15s %zu", type_str, &obj_size) != 2) {
        free(buf);
        return -1;
    }

    if (strcmp(type_str, "blob") == 0) *type_out = OBJ_BLOB;
    else if (strcmp(type_str, "tree") == 0) *type_out = OBJ_TREE;
    else if (strcmp(type_str, "commit") == 0) *type_out = OBJ_COMMIT;
    else {
        free(buf);
        return -1;
    }

    if (header_len + obj_size > (size_t)size) {
        free(buf);
        return -1;
    }

    void *data = malloc(obj_size);
    if (!data) {
        free(buf);
        return -1;
    }

    memcpy(data, buf + header_len, obj_size);

    *data_out = data;
    *len_out = obj_size;

    free(buf);
    return 0;
}
