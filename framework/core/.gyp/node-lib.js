const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const lib = {
  exitOnError: function (error) {
    console.error(error);
    process.exit(-1);
  },
  getConfigValue: function (name) {
    return process.env[`npm_package_config_${name}`];
  },
  run: function (cmd, argv = [], check = true, silent = false) {
    const real_cwd = fs.realpathSync(path.resolve(process.cwd()));
    if (!silent) {
      console.log(`$ ${cmd} ${argv.join(' ')}`);
    }
    const result = spawnSync(cmd, argv, {
      shell: true,
      stdio: 'inherit',
      windowsHide: true,
      cwd: real_cwd,
    });
    if (check && result.status !== 0) {
      process.exit(result.status);
    }
    return result;
  },
  hasTool: function (tool) {
    return lib.run(tool, ['--version'], false).status === 0;
  },
};

module.exports = lib;
