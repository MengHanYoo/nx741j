# Disabled workflows

Per the device owner's request, no GitHub Actions workflow may run for this repository.
GitHub only registers files under `.github/workflows/`, so the two workflows live here:

- `porting-record.yml` — the official record check (`Device documentation`); it used to run on
  every push and pull request.
- `package.yml` — the manual image build (`Device image`).

Restore by moving a file back:

```sh
git mv .github/workflows-disabled/porting-record.yml .github/workflows/porting-record.yml
```

Consequence: the published device repository no longer satisfies the "Porting record check
passes" requirement of the official publish step until the workflow is enabled again.
