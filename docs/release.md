# Release Process

This project is prepared for GitHub, but no public repository is created or
pushed automatically. Confirm the GitHub account and repository name first.

## Normal Development Flow

1. Create a branch from `main`.
2. Make a small, reviewable change.
3. Run `make TOOLCHAIN=llvm`.
4. Run `make smoke TOOLCHAIN=llvm`.
5. Open a pull request using the template.
6. Merge after CI passes.

## v0.1.0 Release Checklist

1. Make sure the changelog describes the release.
2. Build locally: `make clean && make TOOLCHAIN=llvm`.
3. Smoke test locally: `make smoke TOOLCHAIN=llvm`.
4. Tag the release:

   ```sh
   git tag -a v0.1.0 -m "MyOS v0.1.0 first boot"
   git push origin main
   git push origin v0.1.0
   ```

5. Create a GitHub release named `MyOS v0.1.0`.
6. Attach `build/myos-x86_64.iso` from a trusted local or CI build.

## Creating the GitHub Repository Later

After choosing the account and repository name:

```sh
git remote add origin git@github.com:ACCOUNT/REPOSITORY.git
git branch -M main
git push -u origin main
```

