



document.getElementById("wifiForm").addEventListener("submit", function(e) {
    e.preventDefault();
    const formData = new FormData(e.target);
    fetch("/set_wifi", {
        method: "POST",
        body: JSON.stringify({
            ssid: formData.get("ssid"),
            password: formData.get("password")
        }),
        headers: {
            "Content-Type": "application/json"
        }
    })
    .then(r => r.text())
    .then(alert)
    .catch(console.error);
});