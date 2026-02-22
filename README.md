# arkify

Arkify allows building Fedora/RHEL-like SRPMs from Linux kernel Git clones
by adding a slightly modified variant of the "kernel-ark" infrastructure used
by Fedora/RHEL to maintain its kernels.

Note: arkify is in a sort or 'alpha' state: if covers all the important
use-cases, but still need some polish and lacks constant testing, as its unclear
if there is any interest.

## Quick start

```
# cd /to/some/git/repository/with/a/Linux/git/clone/
curl --silent 'https://gitlab.com/knurd42/linux/-/raw/arkify-arkify/arkify' | bash
make dist-srpm
mock redhat/rpm/SRPMS/kernel-*.src.rpm
```

Instead of mock you can also use rpmbuild, koji or copr to build the SRPM.

To update the arkify infrastructure at a later point, run
`curl --silent 'https://gitlab.com/knurd42/linux/-/raw/arkify-arkify/arkify' | bash`
again.

## How arkify works

When you execute arkify for the first time, it will do the following things
(assuming your current branch is called 'master' and based on Linux mainline):

* Add [gitlab.com/knurd42/linux.git](https://gitlab.com/knurd42/linux.git) as a
  new remote 'arkify'.
* Fetch required remote branches from said remote.
* Create a 'arkify-local-upstream-master' branch containing Linux mainline at
  the point where your local 'master' branch forked off.
* Create a 'arkify-local-infra-master' branch from a tag in the
  'arkify/arkify-infra-mainline' branch that is close to the date of the HEAD
  commit in your 'master' branch, as anything older or newer might be unsuitable
  and result in a build error; if you nevertheless want the latest arkify
  infrastructure similar to the one used for Fedora rawhide currently, start
  arkify with '--latest'.
* Switch to 'arkify-local-infra-master' branch.
* Adjust the configuration in 'redhat/Makefile.variables' to local needs.
* Checkout 'master' brach.
* Bulk-import the arkify infrastructure from 'arkify-local-infra-master' using
  'git archive --format=tar arkify-local-infra-master redhat/ […] | tar -x';
  this will steer clear of any patches that Red Hat added to the
  [os-build branch of kernel-ark](https://gitlab.com/cki-project/kernel-ark),
  which 'arkify/arkify-infra-mainline' is based on.
* Add a hook to 'Makefile' enabling the arkify infrastructure.
* Commit the imported arkify infrastructure.
* Warn user if any fixes might be required for the build to succeed.

That arkify infrastructure imported from 'arkify-local-infra-master' contains
everything needed to build the SRPM with 'make dist-srpm', which among other
requires a spec file template (redhat/kernel.spec.template) and the bits to
create configuration files for various archs and kernel variants. These bits
are nearly identical to the [kernel-ark infrastructure](https://gitlab.com/cki-project/kernel-ark)
([documentation](https://cki-project.gitlab.io/kernel-ark/)) used by Fedora
to build the SRPMs for its kernels, but disables a few things (see
'Advantages of using arkify over kernel-ark/ark-latest or kernel-ark/ark-infra'
below for details).

Running arkify again later will update 'arkify-local-upstream-master' and rebase
'arkify-local-infra-master' to a suitable upstream point; afterwards it will
bulk-import the code from the latter to 'master'. This will overwrite any
modifications you performed to the ark infrastructure (e.g. the redhat/
directory) in 'master'. To prevent that, perform them in 'arkify-infra-mainline'
branch instead; afterwards checkout 'master' and run arkify again to import your
changes. That way arkify then can cleanly rebase your changes on-top of the
upstream changes to the arkify/ark infrastructure using the normal Git merge
mechanisms.

Note, arkify will create 'arkify-infra-…' branches for each local branch you use
arkify on. If you add local changes to say 'arkify-infra-master' branch, you thus
might need to cherry-pick them into the others; this is required, as the state of
the arkify infrastructure to build mainline from ten weeks ago might be unsuitable
to build current mainline or vice versa.

Be aware that arkify avoids build sub-packages like -tools, -perf, or -selftests;
it also does not build a -debug or -rt kernel. If you need any of that, enable
it locally in 'redhat/kernel.spec.template' before building a SRPM.

## Advantages of using arkify over kernel-ark/os-build or kernel-ark/ark-infra

Arkify and the SRPMs it creates have the following advantages over using
[kernel-ark/os-build](https://gitlab.com/cki-project/kernel-ark/-/tree/os-build):

* The SRPMS build just one kernel. This speeds things up a lot, as it avoids
  building various things you often won't need:
  - A separate debug kernel.
  - The efiuki sub-package.
  - Utilities shipped in sub-packages like -tools, -perf, or -selftests.
* The SRPMs besides rawhide work on all current Fedora Linux releases.
* Arkify support Linux-next and various Linux stable series.
* The SRPM are generated faster, as they try less hard compressing the kernel
  sources during tarball generation.
* Arkify applies a proven state of the ark infrastructure that should match your
  code base better than the latest ark-infrastructure from kernel-ark.
* Arkify suggests fixes found in [kernel-ark/ark-latest](https://gitlab.com/cki-project/kernel-ark/-/tree/ark-latest)
  that you might have to cherry pick to make the SRPM compile.

## TODO list

See the [header of arkify](https://gitlab.com/knurd42/linux/-/raw/arkify-arkify/arkify).

## Submitting improvements

For any improvements to the arkify script, feel free to open a merge request in
[gitlab.com/knurd42/linux.git](https://gitlab.com/knurd42/linux.git). You
normally do not want to submit improvements to the ark infrastructure there if
they are specific to arkify use; submit all other changes regarding the ark
infrastructure [to kernel-ark instead](https://gitlab.com/cki-project/kernel-ark),
as they most likely will be useful for Fedora and/or kernel-ark in general.
Once merged there they within a day or two will be picked up by arkify.

## License

Arkify was started by Thorsten Leemhuis and is available under the MIT license
– a permissive free software license which puts only very limited restrictions
on reuse.

