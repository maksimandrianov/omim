final class WhatsNewController: WelcomeViewController {

  private struct WhatsNewConfig: WelcomeConfig {
    let image: UIImage
    let title: String
    let text: String
    let buttonTitle: String
    let ctaButtonTitle: String?
    let ctaButtonUrl: String?
  }
  
  static var welcomeConfigs: [WelcomeConfig] {
  return [
    WhatsNewConfig(image: #imageLiteral(resourceName: "img_whats_new_leave_review"),
                   title: "whats_new_ugc_title_updated_v840",
                   text: "whats_new_leave_review",
                   buttonTitle: "done",
                   ctaButtonTitle: nil,
                   ctaButtonUrl: nil)
    ]
  }

  override class var key: String { return welcomeConfigs.reduce("\(self)", { return "\($0)_\($1.title)" }) }
  
  static var shouldShowWhatsNew: Bool {
    get {
      return !UserDefaults.standard.bool(forKey: key)
    }
    set {
      UserDefaults.standard.set(!newValue, forKey: key)
    }
  }

  static func controllers() -> [WelcomeViewController] {
    var result = [WelcomeViewController]()
    let sb = UIStoryboard.instance(.welcome)
    WhatsNewController.welcomeConfigs.forEach { (config) in
      let vc = sb.instantiateViewController(withIdentifier: toString(self)) as! WelcomeViewController
      vc.pageConfig = config
      result.append(vc)
    }
    return result
  }
  
  @IBOutlet weak var ctaButton: UIButton!
  
  override func viewDidLoad() {
    super.viewDidLoad()
    let config = pageConfig as! WhatsNewConfig
    if let ctaTitleKey = config.ctaButtonTitle {
      ctaButton.setTitle(L(ctaTitleKey), for: .normal)
    } else {
      ctaButton.isHidden = true
    }
  }
  
  @IBAction func onCta() {
    let config = pageConfig as! WhatsNewConfig
    if let url = URL(string: config.ctaButtonUrl!) {
      UIApplication.shared.openURL(url)
    } else {
      assertionFailure()
    }
    close()
  }
}
