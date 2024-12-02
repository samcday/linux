# arkify

arkify adds additional make targets to any Linux kernel Git repository that
allow building SRPMs pretty close to those used by Fedora to build its kernels.

WARNING: arkify is in a sort or 'alpha' state; it round about does anything
needed, but needs more polish to be more robust; OTOH it might be good enough
already for the use case it tries to cover.

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
  thus fail; if you nevertheless want the latest ark infrastructure (e.g. the
  one used to build the current rawhide kernel), start arkify with '--latest'.
* Switch to 'arkify-local-infra-master'.
* Adjust the configuration in 'redhat/Makefile.variables' to local needs.
* Checkout 'master'.
* Bulk-import the ark infrastructure from 'arkify-local-infra-master' using
  'git archive --format=tar arkify-local-infra-master redhat/ […] | tar -x';
  this will steer clear of any patches that Red Hat added to the
  [os-build branch of kernel-ark](https://gitlab.com/cki-project/kernel-ark),
  which 'arkify/arkify-infra-mainline' is based on.
* Add a hook to 'Makefile' enabling the ark infrastructure.
* Commit the imported ark infrastructure.
* Check and warn if any fixes might be required for the build to succeed.

That ark infrastructure contains everything needed to build the SRPM with
'make dist-srpm', which among other requires a spec file template
(redhat/kernel.spec.template) and the bits to create configuration files for
various archs and kernel variants; see the [kernel-ark documentation](https://cki-project.gitlab.io/kernel-ark/)
and its [repository](https://gitlab.com/cki-project/kernel-ark) for details.

Running arkify again later will when needed update 'arkify-local-upstream-master'
and rebase 'arkify-local-infra-master' to a suitable upstream point; afterwards
it will bulk-import the code from the latter to 'master'. This will overwrite
any modifications you performed to the ark infrastructure in 'master' (e.g.
the redhat/ directory). To prevent that, perform them in 'arkify-infra-mainline'
branch instead; afterwards checkout 'master' and run arkify again to import your
changes. That way arkify then can later cleanly rebase your changes on-top of
the upstream changes to the ark infrastructure using the normal Git merge
mechanisms.

Note, arkify will create 'arkify-infra-…' branches for each local branch you use
arkify on. If you add local changes to one 'arkify-infra-…' branch, you thus
might need to cherry-pick then into another; this is required, as the state of
the ark infrastructure to build mainline from ten weeks ago might be unsuitable
to build current mainline or vice versa.

## Advantages of using arkify over kernel-ark/ark-latest or kernel-ark/ark-infra

Arkify and the SRPMs creates with the infra it adds have the following
advantages over using [kernel-ark/ark-latest](https://gitlab.com/cki-project/kernel-ark/-/tree/ark-latest)
and [kernel-ark/ark-infra](https://gitlab.com/cki-project/kernel-ark/-/tree/ark-infra):

* Focus on building just one kernel. This speeds things up a lot, as it avoids
  building various things:
  - The sub-packages with a second kernel that has various debug options enabled.
  - The efiuki sub-package.
  - Utilities shipped in sub-packages like -tools, -perf, or -selftests.
  If you need any of this, re-enable it locally in 'redhat/kernel.spec.template'.
* Support for proper Fedora Linux releases (e.g. anything not rawhide).
* Support for Linux-next and various Linux stable series.
* Faster SRPM generation:
  - Using multiple threads by default for compressing and config generation.
  - Try less hard compressing the kernel sources during tarball generation.
* Use a proven state of things instead of using the latest ark-infrastructure
  and hoping that it will be suitable for your code base, which might be behind
  or ahead of things.
* Suggestion of fixes that might need to be cherry picked to make the kernel
  compile. 

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

