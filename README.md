# arkify

arkify adds additional make targets to any Linux kernel Git repository that
allow building Fedora SRPMs.

The code realizing this is a slightly modified variant of the kernel-ark
infrastructure the Fedora project uses to build all its kernels; packages built
from the SRPMs created with it thus should fit well into a modern Fedora Linux.

WARNING: arkify is early "work in progress"; it might forever remain in that
stage if no community interest in it surfaces over time. See the arkify's
header for a poor man's TODO list.

## Get started

```
curl --silent 'https://gitlab.com/knurd42/linux/-/raw/arkify-arkify/arkify' | bash
make dist-srpm
mock redhat/rpm/SRPMS/kernel-*.src.rpm
```

Instead of mock you can also use rpmbuild, koji or copr to build the SRPM.

To update the ark infrastructure Run `curl --silent 'https://gitlab.com/knurd42/linux/-/raw/arkify-arkify/arkify' | bash`
at a later point.

## How arkify works

When you execute arkify for the first time, it will do the following things:

* Add [gitlab.com/knurd42/linux.git](https://gitlab.com/knurd42/linux.git) as a
  new remote 'arkify'.
* Fetch some of the branches in the newly added remote.
* Create a arkify-upstream-<target_branchname> branch containing mainline at
  the point where your current branch forked off.
* Create a arkify-infra-mainline branch from arkify/arkify-infra-mainline.
* Checkout arkify-infra-mainline.
* Adjust the configuration in redhat/Makefile.variables to local needs.
* Checkout the target branch (e.g. the one checked out before calling arkify).
* Bulk-import the ark infrastructure from arkify-infra-mainline using
  'git archive --format=tar arkify-infra-mainline redhat/ [...] | tar -x';
  this will steer clear of any patches that Red Hat added to the
  [os-build branch of kernel-ark](https://gitlab.com/cki-project/kernel-ark),
  which is the upstream of arkify/arkify-infra-mainline branch.
* Add a hook to the Makefile that enables the ark infrastructure.
* Commit the imported ark infrastructure to the target branch.

That ark infrastructure contains everything needed to build the SRPM, among it
a spec file template (redhat/kernel.spec.template) and the bits to create
configuration files for various arch and kernel variants; see the
[kernel-ark documentation](https://cki-project.gitlab.io/kernel-ark/) and its
[repository](https://gitlab.com/cki-project/kernel-ark)) for details.

Running arify again later will update arkify-upstream-<target_branchname> and
arkify-infra-mainline, to then import the code from the latter to the target
branch. When doing so it will bulk-import the code again just like on the first
import. This will overwrite any modifications you performed to the ark
infrastructure in the target branch (e.g. the redhat/ directory). To prevent
that, perform them in the arkify-infra-mainline branch instead; afterwards
checkout the target branch and run arkify again. That way arkify then can later
merge your changes with the upstream changes to the ark infrastructure using the
normal Git merge mechanisms.

## Why not use kernel-ark/os-build or kernel-ark/ark-infra directly

Using arkify-infra-* branches from [gitlab.com/knurd42/linux.git](https://gitlab.com/knurd42/linux.git)
as base has the following advantages.

* They disable a few time consuming things by default, among them building a
  second kernel variant that has various debug options enabled, the efiuki
  package, and subpackages like -tools, -perf, -selftests, -bpftool et. al.; if
  you need any of this, re-enable them in redhat/kernel.spec.template.
* Speed up SRPM generation by using multiple threads by default for compressing
  and config generation; also try less hard compressing the kernel sources
  during tarball generation.
* Improved support for older Fedora releases.

Downsides:

* No support for RHEL.

## Submitting improvements

For any improvements to arkify script, feel free to open a merge request in
[gitlab.com/knurd42/linux.git](https://gitlab.com/knurd42/linux.git). You
can also submit improvements there; but if it is anything that most
likely will be useful for Fedora and/or kernel-ark in general, please
[submit them to kernel-ark instead](https://gitlab.com/cki-project/kernel-ark).
Once merged there they within a day or two will land in arkify- branches as well.

## License

Arkify was started by Thorsten Leemhuis and is available under the MIT license
– a permissive free software license which puts only very limited restrictions
on reuse.

