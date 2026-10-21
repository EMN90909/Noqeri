# NQDB1 / NoqeriDB legacy compatibility

The former embedded NoqeriDB interpreter, its `.nqd` scripts and NQDB1 `.nqdb` snapshots are maintained only for historic data access/tests. They are a *distinct binary format* from NoeSQL NQDB0002 `.noedb`.

Use the explicit deprecated command `noqeri db-legacy old.nqd old.nqdb` for historical scripts. It is not a NoeSQL client and its output must not be renamed to `.noedb`.

There is no verified automated NQDB1-to-NoeSQL converter. Back up legacy files, design a schema-specific export/import conversion, and validate row counts, types, constraints and application queries before cutover.
