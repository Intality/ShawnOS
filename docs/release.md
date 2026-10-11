# Release Process

The project repository is [Intality/ShawnOS](https://github.com/Intality/ShawnOS).
The current kernel and ISO filenames retain the temporary MyOS name.

## Normal Development Flow

1. Create a branch from `main`.
2. Make a small, reviewable change.
3. Run `make TOOLCHAIN=llvm`.
4. Run `make test TOOLCHAIN=llvm` and the 640x480 commands in the README.
5. Open a pull request using the template.
6. Merge after CI passes.

## v0.2.0 Release Checklist

1. Make sure the changelog describes the release.
2. Build locally: `make clean && make TOOLCHAIN=llvm`.
3. Test locally: `make test TOOLCHAIN=llvm`, then run the 640x480 desktop test.
4. Confirm GitHub Actions CI passes for the exact `main` commit to be released.
5. Tag the release:

   ```sh
   git tag -a v0.2.0 -m "ShawnOS v0.2.0 kernel desktop"
   git push origin main
   git push origin v0.2.0
   ```

6. Create a GitHub release named `ShawnOS v0.2.0`.
7. Attach `build/myos-x86_64.iso` from a trusted local or CI build.

## Working With the Repository

Clone the existing repository in a new development environment:

```sh
git clone https://github.com/Intality/ShawnOS.git
cd ShawnOS
```

For the existing local project, `origin` is already configured. Push a development
branch and open a pull request; upload `main` only after review and passing CI.
The ISO is a release asset or CI artifact, not a tracked source file.

