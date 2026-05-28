module.exports = [
  {
    files: ["src/pkjs/**/*.js"],
    languageOptions: {
      ecmaVersion: 5,
      sourceType: "commonjs",
      globals: {
        clearTimeout: "readonly",
        console: "readonly",
        encodeURIComponent: "readonly",
        localStorage: "readonly",
        Pebble: "readonly",
        setInterval: "readonly",
        setTimeout: "readonly",
        WebSocket: "readonly",
        XMLHttpRequest: "readonly"
      }
    },
    rules: {
      "no-undef": "error",
      "no-unused-vars": ["error", { "args": "none", "caughtErrors": "none" }]
    }
  }
];
