# Jack 2 custom build

Build latest jack2 packages on rpi.

```shell
sudo apt install git-buildpackage cdbs libsamplerate-dev libreadline-dev libraw1394-dev libffado-dev libopus-dev libsystemd-dev libasound2-dev libdb-dev
mkdir -p local/src/jack2
cd local/src/jack2
git clone -b debian/1.9.17_dfsg-1 https://salsa.debian.org/multimedia-team/jackd2.git
cd jackd2
gbp buildpackage --git-ignore-branch --git-no-sign-tags
```

*NOTE* the packages get built but gdp wants to sign them, that will fail, its no problem, we sign them later.

To test the packages out on pi before deploying

write the `~pi/.aptly.conf` file:

```
{
  "rootDir": "/home/pi/.aptly",
  "downloadConcurrency": 4,
  "downloadSpeedLimit": 0,
  "architectures": [],
  "dependencyFollowSuggests": false,
  "dependencyFollowRecommends": false,
  "dependencyFollowAllVariants": false,
  "dependencyFollowSource": false,
  "dependencyVerboseResolve": false,
  "gpgDisableSign": false,
  "gpgDisableVerify": false,
  "gpgProvider": "gpg",
  "downloadSourcePackages": false,
  "skipLegacyPool": true,
  "ppaDistributorID": "ubuntu",
  "ppaCodename": "",
  "skipContentsPublishing": false,
  "FileSystemPublishEndpoints": {
    "testjack": {
      "rootDir": "/opt/srv/testjack",
      "linkMethod": "copy",
      "verifyMethod": "md5"
    }
  },
  "S3PublishEndpoints": {},
  "SwiftPublishEndpoints": {}
}
```
example pw: `g7R^*B85FRpiX`

```shell
sudo apt install gnupg1
gpg1 --gen-key
gpg1 --export --armor --output xnor-gpg.pub
sudo apt-key add xnor-gpg.pub
aptly -distribution=buster repo create -component="extra" extra
aptly repo add extra ..
aptly publish repo extra testjack
```

or, to update
```
aptly publish update buster
```

with tmux or screen
```
aptly serve
```

add to sources.list


```shell
sudo vi /etc/apt/sources.list.d/testjack.list
```

```
# testjack/buster [armhf, source] publishes {main: [jack]}
deb http://xnor-rnbo-rpi:8080/testjack/ buster main
deb-src http://xnor-rnbo-rpi:8080/testjack/ buster main
```

```shell
sudo apt update
sudo apt-get install jackd2 libjack-jackd2-dev
```
