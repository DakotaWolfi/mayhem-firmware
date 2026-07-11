# Pull Only From Upstream

This guide shows how to update your local branch from the upstream repository only.

## 1) Check remotes

```powershell
git remote -v
```

You should have both remotes:
- `origin` = your fork
- `upstream` = official repo

If `upstream` is missing, add it:

```powershell
git remote add upstream https://github.com/portapack-mayhem/mayhem-firmware.git
```

## 2) Fetch upstream only

```powershell
git fetch upstream
```

This downloads refs from upstream without touching your working files.

## 3) Update your current branch from upstream/next

If you want a merge commit:

```powershell
git merge upstream/next
```

If you prefer a linear history:

```powershell
git rebase upstream/next
```

## 4) One-command pull from upstream only

Use this instead of `git pull`:

```powershell
git pull upstream next
```

This pulls from upstream branch `next` only.

## 5) Make upstream your default pull remote (optional)

Run this once while on your branch:

```powershell
git branch --set-upstream-to=upstream/next next
```

After this, plain `git pull` on `next` will pull from upstream.

## 6) Verify what changed

```powershell
git log --oneline --decorate --graph -20
```

## 7) Push to your fork only when you choose

```powershell
git push origin next
```

This sends your updated local branch to your fork, but does not change where pull comes from.

---

## Safe workflow summary

```powershell
git fetch upstream
git checkout next
git rebase upstream/next
# or: git merge upstream/next
git push origin next
```

## Avoid accidental pulls from origin

- Do not run `git pull origin next` unless you explicitly want fork changes.
- Prefer `git fetch upstream` + `git rebase upstream/next`.
