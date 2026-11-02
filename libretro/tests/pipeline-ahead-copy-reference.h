/* Frozen from 5daa1d5; LGPL-2.1-or-later. */
    PipelineKey *key = &job->key;
    memcpy(key, draw_key, sizeof(*key));
    memcpy(&key->shader_state, &r->ahead_state, sizeof(ShaderState));
