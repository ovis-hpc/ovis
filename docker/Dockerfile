# SYNOPSIS
#     docker build -t ovis-centos-build -f Dockerfile
FROM rockylinux:8

# Do everything in one RUN to save image size as each RUN commit into a docker
# image layer. Since each RUN results in a layer, even though we perform a RUN
# for a clean up at the end, the stuff still live in the layer.
#
# For comparison:
# - single RUN size: ~ 750 MB,
# - Multiple RUN (RUN for each command): ~ 2.7 GB.
RUN /bin/bash -c '\
set -e ; \
set -x ; \
sed -i "s/^tsflags=nodocs/# tsflags=nodocs/" /etc/yum.conf ; \
yum -y upgrade ; \
yum -y reinstall $(rpm -qa) ; \
yum -y install man ; \
yum -y install epel-release ; \
yum -y install dnf-plugins-core ; \
yum config-manager --set-enabled powertools ; \
yum -y install which openssh-server gdb gdb-gdbserver iproute \
               openssl openssl-devel vim readline-devel \
	       perl-ExtUtils-MakeMaker pam-devel mariadb-devel libcurl-devel \
	       python3-devel papi-devel libpfm-devel tmux \
	       bzip2-devel \
	       sudo libibverbs-devel libfabric-devel \
	       libuuid-devel \
	       librdkafka-devel \
	       slurm slurm-devel \
	       munge munge-devel \
	       ; \
yum -y group install "Development Tools" ; \
yum clean all ; \
'

# Keys
RUN /bin/bash -c '\
yes "" | ssh-keygen ; \
cat /root/.ssh/id*.pub > /root/.ssh/authorized_keys ; \
cd /etc/ssh ; \
echo "---- Generate sshd keys ----" ; \
yes "" | ssh-keygen -t rsa -f ssh_host_rsa_key ; \
yes "" | ssh-keygen -t ecdsa -f ssh_host_ecdsa_key ; \
yes "" | ssh-keygen -t ed25519 -f ssh_host_ed25519_key ; \
echo "---- Generate munge key ----" ; \
dd if=/dev/zero of=/etc/munge/munge.key bs=4096 count=1 \
    && chmod 600 /etc/munge/munge.key \
    && chown munge:munge /etc/munge/munge.key ; \
'

RUN /bin/bash -c '\
yes | pip3 install --upgrade pip ; \
yes | pip3 install Cython ; \
yes | pip3 install numpy ; \
yes | pip3 install etcd3 ; \
yes | pip3 install PyYAML ; \
yes | pip3 install ipython ; \
yes | pip3 install pandas ; \
'

RUN /bin/echo >/etc/tmux.conf -e "\
bind C-h select-pane -L\n\
bind C-j select-pane -D\n\
bind C-k select-pane -U\n\
bind C-l select-pane -R\n\
\n\
# gnome-terminal title\n\
set-option -g set-titles-string \"#(whoami)@#H - tmux\"\n\
set-option -g set-titles on\n\
\n\
# disable bell\n\
set-option -g bell-action none\n\
\n\
set -s escape-time 0\n\
\n\
set-window-option -g mode-keys vi\n\
bind C-[ copy-mode\n\
\n\
set-option -g display-panes-time 60000\n\
\n\
unbind-key Space\n\
"

RUN /bin/echo >/etc/profile.d/prompt.sh -e "\
myprompt() {\n\
	if test -z \"\$parent\"; then\n\
		if ((PPID)); then\n\
			parent=\$(ps -o cmd --no-headers \$PPID | awk '{ print \$1 }')\n\
		else\n\
			parent=\"--\"\n\
		fi\n\
	fi\n\
	if test -z \"\$left\"; then\n\
		if (( \$UID )); then\n\
			# This is regular user\n\
			left='\${debian_chroot:+(\$debian_chroot)}\[\\\\033[01;32m\]\u\[\\\\033[01;33m\]@\h\[\\\\033[01;34m\] \w'\n\
		else\n\
			# This is root\n\
			left='\${debian_chroot:+(\$debian_chroot)}\[\\\\033[01;31m\]\u\[\\\\033[01;33m\]@\h\[\\\\033[01;34m\] \w'\n\
		fi\n\
	fi\n\
\n\
	_branch=\"\$(git symbolic-ref HEAD --short 2>/dev/null || echo '--')\"\n\
	_git_sym_ref=\"\\\\033[0;32m(git-branch: \$_branch)\"\n\
\n\
	right=\"\\\\[\\\\033[01;31m\\\\][\$parent]\"\n\
	[[ \"\$TERM\" = \"linux\" ]] || echo -ne \"\\\\033]0;\${USER}@\${HOSTNAME}:\$PWD\\\\007\"\n\
	PS1=\$(printf \"\\\\n%s %s\\\\n%s\\\\n\\\\[\\\\033[01;34m\\\\]\\\\$\\\\[\\\\033[00m\\\\] \" \"\$right\" \"\$_git_sym_ref\" \"\$left\" )\n\
}\n\
export -f myprompt\n\
export PROMPT_COMMAND=myprompt\n\
"

RUN /bin/echo >/etc/profile.d/less_color.sh -e "\
export LESS_TERMCAP_mb=\$'\\\\E[01;31m'       # begin blinking\n\
export LESS_TERMCAP_md=\$'\\\\E[01;38;5;74m'  # begin bold\n\
export LESS_TERMCAP_me=\$'\\\\E[0m'           # end mode\n\
export LESS_TERMCAP_se=\$'\\\\E[0m'           # end standout-mode\n\
export LESS_TERMCAP_so=\$'\\\\E[38;5;016m\\\\E[48;5;220m'    # begin standout-mode - info box\n\
export LESS_TERMCAP_ue=\$'\\\\E[0m'           # end underline\n\
export LESS_TERMCAP_us=\$'\\\\E[04;38;5;146m' # begin underline\n\
"

RUN /bin/bash -c "\
yum install -y pssh ; \
yum clean all ; \
"
