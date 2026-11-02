/* Copyright (c) 2021-2024 Matt Borgerson */
/* Frozen ordinary lookup from 6ceae02 for code-generation and behavior controls. */
static inline
LruNode *reference_lookup(Lru *lru, uint64_t hash, const void *key)
{
	unsigned int bin = lru_hash_to_bin(lru, hash);
	LruNode *iter, *found = NULL;

	QTAILQ_FOREACH(iter, &lru->bins[bin], next_bin) {
        if ((iter->hash == hash) && !lru->compare_nodes(lru, iter, key)) {
            found = iter;
            break;
        }
    }

	bool bin_head = found && found == QTAILQ_FIRST(&lru->bins[bin]);
	if (found) {
		if (!bin_head) {
			QTAILQ_REMOVE(&lru->bins[bin], found, next_bin);
		}
	} else {
		found = lru_get_one_free(lru);
		found->hash = hash;
		if (lru->init_node) {
			lru->init_node(lru, found, key);
		}
		assert(found->hash == hash);

		lru->num_used += 1;
		lru->num_free -= 1;
	}

	/* A hit can already lead either list independently. Keep its links
	 * untouched when no recency change is needed. */
	if (found != QTAILQ_FIRST(&lru->global)) {
		QTAILQ_REMOVE(&lru->global, found, next_global);
		QTAILQ_INSERT_HEAD(&lru->global, found, next_global);
	}
	if (!bin_head) {
		QTAILQ_INSERT_HEAD(&lru->bins[bin], found, next_bin);
	}

	return found;
}
