# Release Process

The project repository is [Intality/ShawnOS](https://github.com/Intality/ShawnOS).
The current kernel and ISO filenames retain the temporary MyOS name.

## Normal Development Flow

1. Create a branch from `main`.
2. Make a small, reviewable change.
3. Run `make TOOLCHAIN=llvm`.
4. Run `make test TOOLCHAIN=llvm` and the 640x480 commands in the README.
   Review edits and make commits/pushes through the visible Ubuntu VS Code workflow.
5. Open a pull request using the template.
6. Merge after CI passes.

## v0.3.1 Release Checklist

1. Make sure the changelog describes the release.
2. Build locally: `make clean && make TOOLCHAIN=llvm`.
3. Test locally: `make test TOOLCHAIN=llvm`, then run the 640x480 desktop test.
   Fresh-boot the exact release ISO in Windows VirtualBox, not a saved state;
   verify advancing uptime, keyboard, and mouse. Record backend, settings,
   checksum, and any checks still unverified.
4. Confirm GitHub Actions CI passes for the exact `main` commit to be released.
   Remove the changelog's Unreleased label only when approving the release.
5. Tag the release:

   ```sh
   git tag -a v0.3.1 -m "ShawnOS v0.3.1 VirtualBox timer compatibility"
   git push origin main
   git push origin v0.3.1
   ```

6. Create a GitHub release named `ShawnOS v0.3.1`.
7. Attach `build/myos-x86_64.iso` from a trusted local or CI build.
8. Attach its SHA-256 checksum, release notes, exact commit, tool versions, and
   passing test results. Kernel fault-test ISOs are not release assets.

These are publication instructions, not permission for an agent to execute them.
Ask for approval and keep commits/pushes visible in Ubuntu's VS Code workflow.

## Working With the Repository

Clone the existing repository in a new development environment:

```sh
git clone https://github.com/Intality/ShawnOS.git
cd ShawnOS
```

For the existing local project, `origin` is already configured. Push a development
branch and open a pull request; upload `main` only after review and passing CI.
The ISO is a release asset or CI artifact, not a tracked source file.

