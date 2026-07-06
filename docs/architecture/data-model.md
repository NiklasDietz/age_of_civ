# Data model

No database persistence layer. Checked: migration directories (`migrations/`,
`db/migrate/`, `alembic/`, `prisma/`), ORM markers, and `CREATE TABLE` in `*.sql` /
`schema.prisma` / `schema.rb` — none found. Game state persists through the custom
versioned binary save format documented in
[components/save.md](components/save.md) (`src/save/Serializer.cpp`), which is a
serialization surface, not a queryable data model.

<!-- arch-doc: data-model=none; no migrations/ORM/CREATE TABLE found -->
