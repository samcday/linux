# arkify

arkify adds additional make targets to a Linux kernel Git repository to among
others build Fedora SRPMs. The code realizing this is a slightly modified
variant of the kernel-ark infrastructure the Fedora project uses to build all
its kernels; packages built from the SRPMs created with the code arkify adds
thus should fit well into a modern Fedora Linux.

WARNING: arkify is early "work in progress"; it might forever remain in that
stage if no community interest in it surfaces over time.

## Get started

```
curl --silent 'https://gitlab.com/knurd42/linux/-/raw/arkify-arkify/arkify' | bash
make dist-srpm
mock redhat/rpm/SRPMS/kernel-*.src.rpm
```

Instead of mock you can also use rpmbuild, koji or copr to build the SRPM.

## How arkify works

When you execute arkify for the first time, it will do the following things:

* Add [gitlab.com/knurd42/linux.git](https://gitlab.com/knurd42/linux.git) as a
  new remote to your tree as 'arkify'.
* Fetch some of the branches in the newly added remote.
* Create a mainline branch from arkify/mainline.
* Create a arkify-mainline branch from arkify/arkify-mainline.
* Checkout arkify-mainline.
* Adjust the configration in redhat/Makefile.variables.
* Checkout the branch you previously had checked out.
* Bulk-import the ark infrastructure from arkify-mainline using 'git archive
  --format=tar redhat/ [...] | tar -x'; this will steer clear of any patches
  that Red Hat added to the [os-build branch of kernel-ark](https://gitlab.com/cki-project/kernel-ark),
  which is the upstream of the arkify-mainline branch of ark-vanilla.
* Add a hook to the Makefile that enables the ark infrastructure.
* Commit the imported ark infrastructure.

That ark infrastructure contains everything needed to build the SRPM, among it
a spec file template (redhat/kernel.spec.template) and the config files for
various arch and kernel variants; see the [kernel-ark
documentation](https://cki-project.gitlab.io/kernel-ark/) and its
[repository](https://gitlab.com/cki-project/kernel-ark)) for details.

Arify does not support updating the ark infrastructure yet. It will learn that,
but when updating will perform bulk-imports into the target branch just like on
the first import. This will remove any modifications you perform to the ark
infrastructure in the target branch (e.g. the redhat/ directory); to prevent
that, performa them in the arkify-mainline branch instead and cherry-pick them
to your target branch. That way arify then can later merge your changes with
the upstream changes to the ark infrastructure using the normal git mechanisms.

## Why not kernel-ark/os-build directly

Using arkify-* branches from [gitlab.com/knurd42/linux.git](https://gitlab.com/knurd42/linux.git)
as base has the following advantages.

* Disable a few time consuming things by default, among them building a second
  kernel variant that has various debug options enabled, the efiuki package,
  and subpackages like -tools, -perf, -selftests, -bpftool et. al.; if you need
  any of this, reenable them in redhat/kernel.spec.template
* Use multiple threads by default for compressing and config generation.
* Try less hard compressing the kernel sources during tarball generation to
  speed up SRPM generation.
* Improved support for older Fedora releases.

## Poor man's todo list

* Check if the current branch is actually a linux tree.
* Support for updating the infra from ark-infra-mainline
* Is the "mainline" branch actually needed? And should it better be the branch
  the current branch the developer is working on is derived from?
* Support -next.
* Support stable and stable-rc.
* Error handling, including collision detection for the names chosen for the
  remotes and branches we create.

## Submitting improvements

For any improvements to arify script, feel free to open a merge request in
[gitlab.com/knurd42/linux.git](https://gitlab.com/knurd42/linux.git).  You can
also submit improvements there; but be aware that it might make more sense to
[submit them to kernel-ark instead](https://gitlab.com/cki-project/kernel-ark).
That way they will benefit Fedora as well and from there within a day or two
will land in arify- branches as well.

## License

Arkify was started by Thorsten Leemhuis and is available under the MIT license
– a permissive free software license which puts only very limited restrictions
on reuse.

