package com.winpreview.services;

import java.time.Instant;
import java.util.UUID;
import java.util.Objects;

public final class CacheEntry<T> {
    private final String key;
    private final T value;
    private final Instant createdAt;
    private final long ttlSeconds;

    public CacheEntry(String key, T value, long ttlSeconds) {
        this.key = Objects.requireNonNull(key, "Key must not be null");
        this.value = Objects.requireNonNull(value, "Value must not be null");
        this.createdAt = Instant.now();
        this.ttlSeconds = ttlSeconds;
    }

    public boolean isExpired() {
        return Instant.now().isAfter(createdAt.plusSeconds(ttlSeconds));
    }

    public String getKey() { return key; }
    public T getValue() { return value; }
    public Instant getCreatedAt() { return createdAt; }

    @Override
    public String toString() {
        return "CacheEntry{" + "key='" + key + ''' + ", expired=" + isExpired() + '}';
    }
}
