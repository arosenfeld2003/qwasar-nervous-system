use axum::{
    http::StatusCode,
    routing::post,
    Json, Router,
};
use nervous_system_common::AlarmDecision;
use tracing::info;

#[tokio::main]
async fn main() {
    dotenvy::dotenv().ok();
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "webhook_listener=info".into()),
        )
        .with_target(false)
        .init();

    let app = Router::new()
        .route("/alarm", post(handle_alarm));

    let listener = tokio::net::TcpListener::bind("0.0.0.0:8099")
        .await
        .expect("failed to bind to 0.0.0.0:8099");

    info!("📡 Webhook listener running on http://0.0.0.0:8099/alarm");
    info!("waiting for alarm notifications...");

    axum::serve(listener, app)
        .await
        .expect("server error");
}

async fn handle_alarm(Json(alarm): Json<AlarmDecision>) -> (StatusCode, String) {
    let severity_str = format!("{:?}", alarm.severity);
    let alarm_type_str = format!("{:?}", alarm.alarm_type);

    println!(
        "\n💌 [WEBHOOK] Received alarm: {} | {} | {} | {}",
        alarm_type_str, severity_str, alarm.message, alarm.device_id
    );

    (StatusCode::OK, "received".to_string())
}
