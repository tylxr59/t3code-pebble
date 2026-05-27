module.exports = [
  {
    type: "heading",
    defaultValue: "T3 Code"
  },
  {
    type: "section",
    items: [
      {
        type: "input",
        messageKey: "baseUrl",
        label: "T3 server URL",
        description: "Example: http://192.168.1.2:3773/",
        defaultValue: "http://192.168.1.2:3773/"
      },
      {
        type: "input",
        messageKey: "pairingCode",
        label: "Pairing code",
        description: "One-time token from t3 serve or the desktop app.",
        defaultValue: ""
      }
    ]
  },
  {
    type: "submit",
    defaultValue: "Save"
  }
];
