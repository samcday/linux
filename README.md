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
your current branch is called 'master' based on Linux 'mainline':

* Add [gitlab.com/knurd42/linux.git](https://gitlab.com/knurd42/linux.git) as a
  new remote 'arkify'.
* Fetch required branches from 'arkify'.
* Create a 'arkify-upstream-master' branch containing mainline at the point
  where your local 'master' branch forked off.
* Create a 'arkify-infra-master' branch from a tag in the
  'arkify/arkify-infra-mainline' branch that is close to the date of the HEAD
  commit in the master branch, as anything newer might be too new and thus fail.
* Switch to 'arkify-infra-master'.
* Adjust the configuration in 'redhat/Makefile.variables' to local needs.
* Checkout the 'master'.
* Bulk-import the ark infrastructure from 'arkify-infra-master' using
  'git archive --format=tar arkify-infra-master redhat/ [...] | tar -x';
  this will steer clear of any patches that Red Hat added to the
  [os-build branch of kernel-ark](https://gitlab.com/cki-project/kernel-ark),
  which is the upstream of 'arkify/arkify-infra-mainline branch'.
* Add a hook to 'Makefile' which enables the ark infrastructure.
* Commit the imported ark infrastructure to the 'master' branch.

That ark infrastructure contains everything needed to build the SRPM, among it
a spec file template (redhat/kernel.spec.template) and the bits to create
configuration files for various archs and kernel variants; see the
[kernel-ark documentation](https://cki-project.gitlab.io/kernel-ark/) and its
[repository](https://gitlab.com/cki-project/kernel-ark) for details.

Running arify again later will update 'arkify-upstream-master' and
'arkify-infra-master', to then bulk-import the code from the latter to the
'master' branch. This will overwrite any modifications you performed to the ark
infrastructure in the 'master' branch (e.g. the redhat/ directory). To prevent
that, perform them in the 'arkify-infra-mainline' branch instead; afterwards
checkout the 'master' branch and run arkify again to import your changes. That
way arkify then can later cleanly merge your changes with the upstream changes
to the ark infrastructure using the normal Git merge mechanisms.

Note, arkify will create 'arkify-infra-…' branches for each local branch you use
arkify on. If you add local changes to one 'arkify-infra-…' branch, you thus
might need to cherry-pick then into another; this sadly is required, as the 
state of the ark infrastructure to build mainline from ten weeks ago might be
unsuitable to build current mainline or vice versa.

## Why not use kernel-ark/os-build or kernel-ark/ark-infra directly

Using 'arkify-infra-…' branches from [gitlab.com/knurd42/linux.git](https://gitlab.com/knurd42/linux.git)
as base has the following advantages.

* They focus on just the basic kernel and avoid building these things:
  - The sub-package with a second kernel with various debug options.
  - The efiuki sub-package.
  - Other subpackages like -tools, -perf, -selftests, or bpftool.
  If you need any of this, re-enable them in redhat/kernel.spec.template.
* Speed up SRPM generation by using multiple threads by default for compressing
  and config generation; also try less hard compressing the kernel sources
  during tarball generation.
* Improved support for older Fedora releases.

Downsides:

* No support for RHEL.

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

