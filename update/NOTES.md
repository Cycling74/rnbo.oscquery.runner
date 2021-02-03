apt install ruby-dbus

dbus-send --session           \
  --dest=org.freedesktop.DBus \
  --type=method_call          \
  --print-reply               \
  /org/freedesktop/DBus       \
  org.freedesktop.DBus.ListNames

dbus-send --session           \
  --dest=org.freedesktop.DBus \
  --type=method_call          \
  --print-reply               \
  /org/freedesktop/DBus       \
  org.freedesktop.DBus.Introspectable.Introspect

dbus-send --session           \
  --dest=org.ruby.service \
  --type=method_call          \
  --print-reply               \
  /org/ruby/MyInstance       \
  org.freedesktop.DBus.Introspectable.Introspect

dbus-send --session --dest=org.ruby.service --type=method_call /org/ruby/MyInstance org.ruby.SampleInterface.hello string:'soda' string:'test'


dbus-send --system           \
  --dest=org.freedesktop.DBus \
  --type=method_call          \
  --print-reply               \
  /org/freedesktop/DBus       \
  org.freedesktop.DBus.ListNames

dbus-send --system           \
  --dest=com.cycling74.rnbo \
  --type=method_call          \
  --print-reply               \
  /com/cycling74/rnbo       \
  org.freedesktop.DBus.Introspectable.Introspect

dbus-send --system --dest=com.cycling74.Rnbo --type=method_call /com/cycling74/Rnbo com.cycling74.Rnbo.update_rnbo string:'soda'

https://stackoverflow.com/questions/11170118/dbus-systembus-policies
