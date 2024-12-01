# arkify

arkify adds additional make targets to any Linux kernel Git repository that
allow building SRPMs that are pretty close to those used by Fedora to build
its kernels.

WARNING: arkify is early "work in progress"; it might forever remain in that
stage if no community interest in it surfaces over time. See
[arkify's header for a poor man's TODO list](https://gitlab.com/knurd42/linux/-/blob/arkify-arkify/arkify#L8).

## Get started

```
curl --silent 'https://gitlab.com/knurd42/linux/-/raw/arkify-arkify/arkify' | bash
make dist-srpm
mock redhat/rpm/SRPMS/kernel-*.src.rpm
```

Instead of mock you can also use rpmbuild, koji or copr to build the SRPM.

To update the infrastructure, run `curl --silent 'https://gitlab.com/knurd42/linux/-/raw/arkify-arkify/arkify' | bash`
again.

## How arkify works

When you execute arkify for the first time, it will do the following things if
your current branch is called 'master' and based on Linux 'mainline':

* Add [gitlab.com/knurd42/linux.git](https://gitlab.com/knurd42/linux.git) as a
  new remote 'arkify'.
* Fetch required remote branches from 'arkify'.
* Create a 'arkify-local-upstream-master' branch containing mainline at the
  point where your local 'master' branch forked off.
* Create a 'arkify-local-infra-master' branch from a tag in the
  'arkify/arkify-infra-mainline-latest' branch that is close to the date of the
  HEAD commit in your 'master' branch, as anything newer might be too new and
  thus fail; if you nevertheless want the code the current rawhide kernel is
  build from, star arkify with '--latest'.
* Switch to 'arkify-local-infra-master'.
* Adjust the configuration in 'redhat/Makefile.variables' to local needs.
* Checkout 'master'.
* Bulk-import the ark infrastructure from 'arkify-local-infra-master' using
  'git archive --format=tar arkify-local-infra-master redhat/ [...] | tar -x';
  this will steer clear of any patches that Red Hat added to the
  [os-build branch of kernel-ark](https://gitlab.com/cki-project/kernel-ark),
  which 'arkify/arkify-infra-mainline' is based on.
* Add a hook to 'Makefile' enabling the ark infrastructure.
* Commit the imported ark infrastructure to 'master'.
* Check and warn if any fixes might be required for the build to succeed.

That ark infrastructure contains everything needed to build the SRPM, among it
a spec file template (redhat/kernel.spec.template) and the bits to create
configuration files for various archs and kernel variants; see the
[kernel-ark documentation](https://cki-project.gitlab.io/kernel-ark/) and its
[repository](https://gitlab.com/cki-project/kernel-ark) for details.

Running arkify again later will when needed update 'arkify-local-upstream-master'
and rebased 'arkify-local-infra-master' to a suitable upstream point; afterwards
it will bulk-import the code from the latter to 'master'. This will overwrite
any modifications you performed to the ark infrastructure in 'master' (e.g.
the redhat/ directory). To prevent that, perform them in 'arkify-infra-mainline'
branch instead; afterwards checkout 'master' and run arkify again to import your
changes. That way arkify then can later cleanly rebase your changes on-top of
the upstream changes to the ark infrastructure using the normal Git merge
mechanisms.

Note, arkify will create 'arkify-infra-…' branches for each local branch you use
arkify on. If you add local changes to one 'arkify-infra-…' branch, you thus
might need to cherry-pick then into another; this sadly is required, as the 
state of the ark infrastructure to build mainline from ten weeks ago might be
unsuitable to build current mainline or vice versa.

## Why not use kernel-ark/os-build or kernel-ark/ark-infra directly

Using 'arkify-infra-…' branches from [gitlab.com/knurd42/linux.git](https://gitlab.com/knurd42/linux.git)
as base has the following advantages.

* They focus on just the basic kernel thus and avoid building these things:
  - The sub-package with a second kernel with various debug options enabled.
  - The efiuki sub-package.
  - Other subpackages like -tools, -perf, or -selftests.
  If you need any of this, re-enable them in redhat/kernel.spec.template.
* Speed up SRPM generation by using multiple threads by default for compressing
  and config generation; also try less hard compressing the kernel sources
  during tarball generation.
* Improved support for proper releases Fedora Linux (e.g. anything not rawhide).

## TODO list

See the [header of arkify](https://gitlab.com/knurd42/linux/-/raw/arkify-arkify/arkify).

## Submitting improvements

For any improvements to the arkify script, feel free to open a merge request in
[gitlab.com/knurd42/linux.git](https://gitlab.com/knurd42/linux.git). You
can also submit improvements to the ark infrastructure there; but they will be
rejected if it is anything that most likely will be useful for Fedora and/or
kernel-ark in general, as then your should [submit them to kernel-ark instead](https://gitlab.com/cki-project/kernel-ark).
Once merged there they within a day or two will be picked up by arkify.

## License

Arkify was started by Thorsten Leemhuis and is available under the MIT license
– a permissive free software license which puts only very limited restrictions
on reuse.

