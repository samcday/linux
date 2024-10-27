# /bin/bash
#
# Early WIP; missing:
# - ark-infra-mainline-latest is ignored for now
# - anything to update the ark-infra, which is where things get messy
# - anything for next and stable branches

#
# go
#
set -e
current_branch=$(git rev-parse --abbrev-ref HEAD)

#
# add stuff we need
#
git remote add --no-tags -t ark-infra-mainline -t mainline -t ark-scripts ark-vanilla  https://gitlab.com/knurd42/linux.git/
git fetch ark-vanilla 
# create a "mainline" branch, which the ark-infra requires to seperate upstream
# from downstream. FIXME: Would a remote work? IIRC it does not, but I might be
# mistaken (Thorsten)
git branch --track mainline ark-vanilla/mainline

#
# prep and adjust ark-infra in a seperate branch
#
git branch --track ark-infra-mainline ark-vanilla/ark-infra-mainline
git checkout ark-infra-mainline
echo $'\n'"DISTLOCALVERSION ?= .$(whoami)" >> redhat/Makefile.variables
# FIXME: this hardcodes a few things:
#  BUMP_RELEASE (might not be a good idea for this usecase)
#  DISTRO=fedora (breaks RHEL support)
#  VERSION_ON_UPSTREAM=1 (wrong for stable)
sed -i '
	s!^\(BUMP_RELEASE \?.\?=\)\(.*\)!\1no! ;
	s!^\(DIST \?.\?=\)\(.*\)!\1 .fc40! ;
	s!^\(DIST_BRANCH \?.\?=\)\(.*\)!\1 '"${current_branch}"'! ;
	s!^\(DISTRO \?.\?=\)\(.*\)!\1 fedora! ;
	s!^\(PATCHLIST_URL \?.\?=\)\(.*\)!\1 none! ;
	s!^\(RELEASED_KERNEL \?.\?=\)\(.*\)!\11! ;
	s!^\(UPSTREAM_BRANCH \?.\?=\)\(.*\)!\1 '"${current_branch}"'! ;
	s!\(VERSION_ON_UPSTREAM \?.\?=\)\(.*\)!\1 1! ;
       ' redhat/Makefile.variables
git commit -s -m 'local ark-infra configuration' redhat/Makefile.variables

#
# import ark-infra
#
git checkout "${current_branch}"
git archive --format=tar ark-infra-mainline makefile Makefile.rhelver redhat/ .copr | tar -x
# FIXME: is this actually needed?
sed -i '/# We are using a recursive / i include Makefile.rhelver\n' Makefile
git add Makefile makefile Makefile.rhelver redhat/
git add -f .copr redhat/kabi/kabi-module/kabi*
git commit -q -s -m "import ark-infra"
make dist-srpm
echo "now use 'mock redhat/rpm/SRPMS/kernel-*.src.rpm' or something else to build the srpm"
