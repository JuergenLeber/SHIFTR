import sys, re, datetime

PATH_VERSION = './include/Version.h'
MAJOR, MINOR, PATCH, BUILD = 0, 1, 2, 3

# Read existing version.h file
with open(PATH_VERSION, 'r') as reader:
  # Find "MAJOR.MINOR.PATCH+BUILD" from the third line
  reader.readline()
  reader.readline()
  line = re.search(r'"([^"]*)"', reader.readline()).group()[1:-1]
  # Extract old values for MAJOR.MINOR.PATCH+BUILD
  versions = re.split('\.|\+', line)
  # Increment value
  versions[BUILD] = int(versions[BUILD]) + 1

  # Write new version.h file
  with open(PATH_VERSION, 'w') as writer:
    time = datetime.datetime.now()

    datestamp = time.strftime('%Y-%m-%d')
    timestamp = time.strftime('%H:%M')
    version = '%s.%s.%s+%d' % (versions[MAJOR], versions[MINOR], versions[PATCH], versions[BUILD])
    versionFull = version + ' %s %s' % (datestamp, timestamp)

    writer.writelines([
      '#ifndef VERSION_H\n',
      '#define VERSION_H\n',
      '#define VERSION "%s"' % version,
      '\n#define VERSION_MAJOR %s' % versions[MAJOR],
      '\n#define VERSION_MINOR %s' % versions[MINOR],
      '\n#define VERSION_PATCH %s' % versions[PATCH],
      '\n#define VERSION_BUILD %s' % versions[BUILD],
      '\n#define VERSION_DATE "%s"' % datestamp,
      '\n#define VERSION_TIME "%s"' % timestamp,
      '\n#define VERSION_FULL "%s"' % versionFull,
      '\n#endif\n'
    ])

    print('Release: ' + version)