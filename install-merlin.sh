#!/bin/sh

src_dir=$(dirname $0)
pushd "$src_dir" >/dev/null 2>&1
src_dir=$(pwd)
popd >/dev/null 2>&1

nagios_cfg=/opt/monitor/etc/nagios.cfg
dest_dir=/opt/monitor/op5/merlin
db_type=mysql
db_name=merlin
db_user=merlin
db_pass=merlin
batch=
install=db,files,config

raw_sed_version=$(sed --version | sed '1q')
sed_version=$(echo "$raw_sed_version" | sed -e 's/[^0-9]*//' -e 's/[.]//g')
if [ "$sed_version" -lt 409 ]; then
	echo "You need GNU sed version 4.0.9 or above for this script to work"
	echo "Your sed claims to be \"$raw_sed_version\" ($sed_version)"
	exit 1
fi

abort ()
{
	echo "$@"
	echo "Aborting."
	exit 1
}

modify_nagios_cfg ()
{
	if ! grep -q "merlin.so" "$nagios_cfg"; then
		say "Adding merlin.so as eventbroker to nagios"
		sed -i "s#^log_file.*#broker_module=$dest_dir/merlin.so $dest_dir/merlin.conf\\n\\n&#" \
			"$nagios_cfg"
		return 0
	fi

	if grep -q "$dest_dir/merlin.so" "$nagios_cfg"; then
		say "merlin.so is already a registered eventbroker in Nagios"
		return 0
	fi

	say "Updating path to merlin.so in $nagios_cfg"
	sed -i "s#broker_module.*merlin.so.*#broker_module=$dest_dir/merlin.so $dest_dir/merlin.conf#" \
		"$nagios_cfg"
	return 0
}

get_arg ()
{
	expr "z$1" : 'z[^=]*=\(.*\)'
}

db_setup ()
{
	case "$db_type" in
		mysql)
			mysql -e \
			  "GRANT ALL ON $db_name.* TO $db_user@localhost IDENTIFIED BY '$db_pass'"
			mysql -e 'FLUSH PRIVILEGES'
			query="SELECT last_check FROM report_data LIMIT 1"
			if ! mysql $db_name -Be "$query" >/dev/null 2>&1; then
				echo "Creating database $db_name"
				mysqladmin create "$db_name"
				mysql $db_name < $src_dir/db.sql
			fi
			;;
		*)
			echo "Unknown database type '$db_type'"
			echo "I understand only lower-case database types."
			return 0
			;;
	esac
}

macro_subst ()
{
	sed -e "s/@@DBNAME@@/$db_name/g" -e "s/@@DBTYPE@@/$db_type/g" \
		-e "s/@@DBUSER@@/$db_user/g" -e "s/@@DBPASS@@/$db_pass/g" \
		-e "s#@@NAGIOSCFG@@#$nagios_cfg#g" -e "s#@@DESTDIR@@#$dest_dir#g" \
		-e "s#@@SRCDIR@@#$src_dir#g" \
		"$@"
}

ask ()
{
	local question options answer
	question="$1" options="$2" default="$3"
	test "$batch" && { echo "$default"; return 0; }

	while true; do
		echo -n "$question " >&2
		read answer
		case "$answer,$default" in
			"",*)
				answer="$default"
				break
				;;
			",") ;;
			*)
				echo "$options " | grep -q "$answer" && break
				;;
		esac
		echo "Please answer one of '$options'" >&2
	done
	echo "$answer" >&1
}

say ()
{
	test "$batch" || echo "$@"
}

install_files ()
{
	test -d "$dest_dir" || mkdir -p 755 "$dest_dir"
	test -d "$dest_dir/logs" || mkdir -p 775 "$dest_dir/logs"
	test -d "$dest_dir" || { echo "$dest_dir is not a directory"; return 1; }
	macro_subst "$src_dir/example.conf" > "$dest_dir/merlin.conf"
	cp "$src_dir/"merlin{d,.so} "$dest_dir"
	cp "$src_dir/db.sql" "$dest_dir"
	chmod 755 "$dest_dir/merlind"
	chmod 600 "$dest_dir/merlin.conf"
	chmod 644 "$dest_dir/merlin.so"
	if [ $(id -u) -eq 0 ]; then
		cp "$src_dir/init.sh" /etc/init.d/merlind
		chmod  755 /etc/init.d/merlind
	else
		say "Lacking root permissions, so not installing init-script."
	fi
}

missing=
for i in merlin.so db.sql merlind example.conf; do
	if ! test -f "$src_dir/$i"; then
		echo "$src_dir/$i is missing"
		missing="$missing $src_dir/$i"
	fi
done
test "$missing" && abort "Essential files are missing. Perhaps you need to run 'make'?"

while test "$1"; do
	case "$1" in
		--nagios-cfg=*)
			nagios_cfg=$(get_arg "$1")
			;;
		--nagios-cfg)
			shift
			nagios_cfg="$1"
			;;
		--dest-dir=*)
			dest_dir=$(get_arg "$1")
			;;
		--dest-dir)
			shift
			dest_dir="$1"
			;;
		--db-name=*)
			db_name=$(get_arg "$1")
			;;
		--db-name)
			shift
			db_name="$1"
			;;
		--db-type=*)
			db_type=$(get_arg "$1")
			;;
		--db-type)
			shift
			db_type="$1"
			;;
		--db-user=*)
			db_user=$(get_arg "$1")
			;;
		--db-user)
			shift
			db_user="$1"
			;;
		--db-pass=*)
			db_pass=$(get_arg "$1")
			;;
		--db-pass)
			shift
			db_pass="$1"
			;;
		--batch)
			batch=y
			;;
		--install=*)
			install=$(get_arg "$1")
			;;
		--install)
			shift
			install="$1"
			;;
		*)
			echo "Illegal argument. I have no idea what to make of '$1'"
			exit 1
			;;
	esac
	shift
done

if ! $(echo "$install" | grep -e db -e files -e config); then
	echo "You have chosen to install nothing"
	echo "You must pass one or more of 'db,files,config' to --install"
fi

cat << EOF
  Database settings:
    Type     (--db-type): $db_type
    Name     (--db-name): $db_name
    Username (--db-user): $db_user
    Password (--db-pass): $db_pass

  Misc settings:
    Nagios config file  (--nagios-cfg): $nagios_cfg
    Destination directory (--dest-dir): $dest_dir

  You have chosen to install the following components: $install
EOF

case $(ask "Does this look ok? [Y/n]" "ynYN" y) in
	n|N) echo "Aborting installation"; exit 1;;
esac

say
say "Installing"
say

if echo "$install" | grep 'files'; then
	install_files || abort "Failed to install files."
fi
if echo "$install" | grep 'db'; then
	db_setup || abort "Failed to setup database."
fi
if echo "$install" | grep 'config'; then
	modify_nagios_cfg || abort "Failed to modify Nagios config."
fi

say 
say "Installation successfully completed"
say
say "You will need to restart Nagios for changes to take effect"
